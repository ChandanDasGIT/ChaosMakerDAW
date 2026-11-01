#include "jackaudiodevice.h"

#include <algorithm>
#include <cerrno>

JackAudioDevice::~JackAudioDevice()
{
    close();
}

bool JackAudioDevice::open(AudioCallback* callback, int wantedBufferFrames)
{
    close();
    m_error.clear();

    jack_status_t status = JackFailure;
    m_client = jack_client_open("ChaosMaker", JackNoStartServer, &status);
    if (!m_client) {
        m_error = (status & JackServerFailed)
            ? "JACK server is not running (start JACK or PipeWire JACK)."
            : "Could not open a JACK audio client.";
        return false;
    }

    m_callback = callback;
    m_sampleRateHz = static_cast<double>(jack_get_sample_rate(m_client));
    m_bufferFrames.store(static_cast<int>(jack_get_buffer_size(m_client)), std::memory_order_relaxed);
    m_requestedBufferFrames = wantedBufferFrames;
    m_bufferRequestAccepted = wantedBufferFrames <= 0 || m_bufferFrames.load(std::memory_order_relaxed) == wantedBufferFrames;
    m_input = jack_port_register(m_client, "guitar_in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    m_outputs[0] = jack_port_register(m_client, "monitor_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    m_outputs[1] = jack_port_register(m_client, "monitor_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!m_input || !m_outputs[0] || !m_outputs[1]
        || jack_set_process_callback(m_client, &JackAudioDevice::process, this) != 0
        || jack_set_buffer_size_callback(m_client, &JackAudioDevice::bufferSizeChanged, this) != 0
        || jack_set_xrun_callback(m_client, &JackAudioDevice::xrun, this) != 0) {
        m_error = "Could not register the JACK audio ports.";
        close();
        return false;
    }

    if (wantedBufferFrames > 0 && m_bufferFrames.load(std::memory_order_relaxed) != wantedBufferFrames) {
        m_bufferRequestAccepted = jack_set_buffer_size(m_client, static_cast<jack_nframes_t>(wantedBufferFrames)) == 0;
        m_bufferFrames.store(static_cast<int>(jack_get_buffer_size(m_client)), std::memory_order_relaxed);
    }

    jack_on_shutdown(m_client, &JackAudioDevice::shutdown, this);
    if (callback)
        callback->deviceChanged(m_sampleRateHz, m_bufferFrames.load(std::memory_order_relaxed));

    if (jack_activate(m_client) != 0) {
        m_error = "Could not activate the JACK audio client.";
        close();
        return false;
    }

    enumeratePorts();
    return true;
}

void JackAudioDevice::close()
{
    if (m_client) {
        jack_deactivate(m_client);
        jack_client_close(m_client);
    }
    m_client = nullptr;
    m_input = nullptr;
    m_outputs = {};
    m_callback = nullptr;
    m_sampleRateHz = 0.0;
    m_bufferFrames.store(0, std::memory_order_relaxed);
}

double JackAudioDevice::sampleRate() const
{
    return m_sampleRateHz;
}

DeviceLatency JackAudioDevice::latency() const
{
    const int bufferFrames = m_bufferFrames.load(std::memory_order_acquire);
    return {bufferFrames, bufferFrames, m_requestedBufferFrames};
}

bool JackAudioDevice::setBufferSize(int frames)
{
    if (!m_client || frames < 16 || (frames & (frames - 1)) != 0)
        return false;
    if (m_bufferFrames.load(std::memory_order_acquire) == frames) {
        m_requestedBufferFrames = frames;
        m_bufferRequestAccepted = true;
        return true;
    }
    m_bufferRequestAccepted = jack_set_buffer_size(m_client, static_cast<jack_nframes_t>(frames)) == 0;
    if (m_bufferRequestAccepted) {
        m_requestedBufferFrames = frames;
        m_bufferFrames.store(frames, std::memory_order_release);
    }
    return m_bufferRequestAccepted;
}

std::vector<std::string> JackAudioDevice::inputNames() const
{
    return m_inputNames;
}

std::vector<std::string> JackAudioDevice::outputNames() const
{
    return m_outputNames;
}

bool JackAudioDevice::connectInput(const std::string& source)
{
    if (!m_client || !m_input || source.empty()) {
        m_error = "Select an available JACK audio input.";
        return false;
    }
    if (std::find(m_inputNames.begin(), m_inputNames.end(), source) == m_inputNames.end()) {
        m_error = "The selected JACK input is no longer available.";
        return false;
    }
    disconnectAll(m_input, false);
    if (jack_connect(m_client, source.c_str(), jack_port_name(m_input)) != 0) {
        m_error = "JACK could not connect the selected input port.";
        return false;
    }
    m_error.clear();
    return true;
}

bool JackAudioDevice::connectOutput(const std::string& left, const std::string& right)
{
    if (!m_client || !m_outputs[0] || left.empty()) {
        m_error = "Select an available JACK audio output.";
        return false;
    }
    if (std::find(m_outputNames.begin(), m_outputNames.end(), left) == m_outputNames.end()
        || (!right.empty() && std::find(m_outputNames.begin(), m_outputNames.end(), right) == m_outputNames.end())) {
        m_error = "The selected JACK output is no longer available.";
        return false;
    }

    disconnectAll(m_outputs[0], true);
    disconnectAll(m_outputs[1], true);
    if (jack_connect(m_client, jack_port_name(m_outputs[0]), left.c_str()) != 0) {
        m_error = "JACK could not connect the left output port.";
        return false;
    }
    if (!right.empty() && jack_connect(m_client, jack_port_name(m_outputs[1]), right.c_str()) != 0) {
        m_error = "JACK could not connect the right output port.";
        return false;
    }
    m_error.clear();
    return true;
}

int JackAudioDevice::process(jack_nframes_t frames, void* context) noexcept
{
    return static_cast<JackAudioDevice*>(context)->processBlock(frames);
}

int JackAudioDevice::bufferSizeChanged(jack_nframes_t frames, void* context) noexcept
{
    auto* self = static_cast<JackAudioDevice*>(context);
    self->m_bufferFrames.store(static_cast<int>(frames), std::memory_order_release);
    return 0;
}

int JackAudioDevice::xrun(void* context) noexcept
{
    auto* self = static_cast<JackAudioDevice*>(context);
    if (self->m_callback)
        self->m_callback->xrunOccurred();
    return 0;
}

void JackAudioDevice::shutdown(void* context) noexcept
{
    (void)context;
}

int JackAudioDevice::processBlock(jack_nframes_t frames) noexcept
{
    if (!m_client || !m_callback)
        return 0;

    m_inputBuffers[0] = static_cast<float*>(jack_port_get_buffer(m_input, frames));
    for (std::size_t i = 0; i < m_outputs.size(); ++i)
        m_outputBuffers[i] = static_cast<float*>(jack_port_get_buffer(m_outputs[i], frames));

    AudioIO io;
    io.input = {m_inputBuffers.data(), 1, static_cast<int>(frames)};
    io.output = {m_outputBuffers.data(), static_cast<int>(m_outputs.size()), static_cast<int>(frames)};
    io.frames = static_cast<int>(frames);
    m_callback->process(io);
    return 0;
}

void JackAudioDevice::enumeratePorts()
{
    m_inputNames.clear();
    m_outputNames.clear();
    const char** captures = jack_get_ports(m_client, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
    const char** playbacks = jack_get_ports(m_client, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
    const std::string ownPrefix = std::string(jack_get_client_name(m_client)) + ":";
    if (captures) {
        for (std::size_t i = 0; captures[i]; ++i) {
            const std::string name(captures[i]);
            if (!name.starts_with(ownPrefix))
                m_inputNames.push_back(name);
        }
        jack_free(const_cast<char**>(captures));
    }
    if (playbacks) {
        for (std::size_t i = 0; playbacks[i]; ++i) {
            const std::string name(playbacks[i]);
            if (!name.starts_with(ownPrefix))
                m_outputNames.push_back(name);
        }
        jack_free(const_cast<char**>(playbacks));
    }
}

bool JackAudioDevice::disconnectAll(jack_port_t* port, bool portIsSource)
{
    const char** connections = jack_port_get_connections(port);
    if (!connections)
        return true;
    bool success = true;
    for (std::size_t i = 0; connections[i]; ++i) {
        const int result = portIsSource
            ? jack_disconnect(m_client, jack_port_name(port), connections[i])
            : jack_disconnect(m_client, connections[i], jack_port_name(port));
        if (result != 0)
            success = false;
    }
    jack_free(const_cast<char**>(connections));
    return success;
}
