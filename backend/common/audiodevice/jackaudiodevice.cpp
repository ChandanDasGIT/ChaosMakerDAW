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
    for(int i=0;i<inputChannels;++i) {
        const std::string name=i==0 ? "guitar_in" : "track_in_"+std::to_string(i);
        m_inputs[i]=jack_port_register(m_client,name.c_str(),JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput,0);
    }
    m_midiInput = jack_port_register(m_client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    m_outputs[0] = jack_port_register(m_client, "monitor_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    m_outputs[1] = jack_port_register(m_client, "monitor_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!m_midiInput || std::any_of(m_inputs.begin(),m_inputs.end(),[](auto* port){return !port;}) || !m_outputs[0] || !m_outputs[1]
        || jack_set_process_callback(m_client, &JackAudioDevice::process, this) != 0
        || jack_set_buffer_size_callback(m_client, &JackAudioDevice::bufferSizeChanged, this) != 0
        || jack_set_xrun_callback(m_client, &JackAudioDevice::xrun, this) != 0) {
        m_error = "Could not register the JACK audio/MIDI ports.";
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
    m_inputs = {}; m_sources = {};
    m_outputs = {};
    m_midiInput = nullptr;
    m_midiInBuffer.clear();
    m_midiInputNames.clear();
    m_connectedMidiSource.clear();
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
    // JACK hands the hardware's latency on to the ports connected to it: the largest capture
    // latency over our inputs plus the largest playback latency over our outputs (G10).
    const auto largest = [](const auto& ports, jack_latency_callback_mode_t mode) {
        jack_nframes_t most = 0;
        for (auto* port : ports) {
            if (!port) continue;
            jack_latency_range_t range{0, 0};
            jack_port_get_latency_range(port, mode, &range);
            most = std::max(most, range.max);
        }
        return most;
    };
    const auto roundTrip = m_client ? largest(m_inputs, JackCaptureLatency) + largest(m_outputs, JackPlaybackLatency) : 0;
    return {bufferFrames, bufferFrames, m_requestedBufferFrames, int(std::min<jack_nframes_t>(roundTrip, 1u << 20))};
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
    if (!m_client || !m_inputs[0] || source.empty()) {
        m_error = "Select an available JACK audio input.";
        return false;
    }
    if (std::find(m_inputNames.begin(), m_inputNames.end(), source) == m_inputNames.end()) {
        m_error = "The selected JACK input is no longer available.";
        return false;
    }
    disconnectAll(m_inputs[0], false);
    if (jack_connect(m_client, source.c_str(), jack_port_name(m_inputs[0])) != 0) {
        m_error = "JACK could not connect the selected input port.";
        return false;
    }
    m_error.clear();
    return true;
}

int JackAudioDevice::connectInputChannel(const std::string& source)
{
    if(!m_client || std::find(m_inputNames.begin(),m_inputNames.end(),source)==m_inputNames.end()) {
        m_error="The selected JACK input is unavailable."; return -1;
    }
    // Channel zero remains the legacy/default route. Track routes never reconnect it.
    for(int i=1;i<inputChannels;++i) if(m_sources[i]==source) return i;
    for(int i=1;i<inputChannels;++i) if(m_sources[i].empty()) {
        const int result=jack_connect(m_client,source.c_str(),jack_port_name(m_inputs[i]));
        if(result!=0 && result!=EEXIST) { m_error="JACK could not connect a track input."; return -1; }
        m_sources[i]=source; m_error.clear(); return i;
    }
    m_error="All 31 independent JACK input routes are in use."; return -1;
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

    for(int i=0;i<inputChannels;++i)
        m_inputBuffers[i]=static_cast<float*>(jack_port_get_buffer(m_inputs[i],frames));
    for (std::size_t i = 0; i < m_outputs.size(); ++i)
        m_outputBuffers[i] = static_cast<float*>(jack_port_get_buffer(m_outputs[i], frames));

    m_midiInBuffer.clear();
    if (m_midiInput) {
        void* portBuffer = jack_port_get_buffer(m_midiInput, frames);
        if (portBuffer) {
            const jack_nframes_t eventCount = jack_midi_get_event_count(portBuffer);
            for (jack_nframes_t i = 0; i < eventCount; ++i) {
                jack_midi_event_t event;
                if (jack_midi_event_get(&event, portBuffer, i) == 0 && event.size >= 1) {
                    MidiEvent me;
                    me.offset = static_cast<int>(event.time);
                    me.status = event.buffer[0];
                    me.data1 = event.size > 1 ? event.buffer[1] : 0;
                    me.data2 = event.size > 2 ? event.buffer[2] : 0;
                    m_midiInBuffer.add(me);
                }
            }
        }
    }

    AudioIO io;
    io.input = {m_inputBuffers.data(), inputChannels, static_cast<int>(frames)};
    io.output = {m_outputBuffers.data(), static_cast<int>(m_outputs.size()), static_cast<int>(frames)};
    io.midiIn = &m_midiInBuffer;
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
    refreshMidiInputs();                                 // the MIDI sources
}

void JackAudioDevice::refreshMidiInputs()
{
    if (!m_client) return;
    m_midiInputNames.clear();
    const std::string ownPrefix = std::string(jack_get_client_name(m_client)) + ":";
    if (const char** midiCaptures = jack_get_ports(m_client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput)) {
        for (std::size_t i = 0; midiCaptures[i]; ++i) {
            const std::string name(midiCaptures[i]);
            if (!name.starts_with(ownPrefix))
                m_midiInputNames.push_back(name);
        }
        jack_free(const_cast<char**>(midiCaptures));
    }
    // The connected device went away (switched off): forget it, so the next one can take over.
    if (!m_connectedMidiSource.empty()
        && std::find(m_midiInputNames.begin(), m_midiInputNames.end(), m_connectedMidiSource) == m_midiInputNames.end())
        m_connectedMidiSource.clear();
    // Chosen by itself: the first real device. "Midi Through" is the system's loopback port,
    // always there and silent, so it is only used when picked by hand.
    if (m_connectedMidiSource.empty() && !m_midiOffByUser) {
        const auto device = std::find_if(m_midiInputNames.begin(), m_midiInputNames.end(),
            [](const std::string& name) { return name.find("Midi Through") == std::string::npos; });
        if (device != m_midiInputNames.end()) connectMidiInput(*device);
    }
}

bool JackAudioDevice::connectMidiInput(const std::string& source)
{
    if (!m_client || !m_midiInput)
        return false;
    disconnectAll(m_midiInput, false);
    m_connectedMidiSource.clear();
    m_midiOffByUser = source.empty();
    if (source.empty())
        return true;
    if (jack_connect(m_client, source.c_str(), jack_port_name(m_midiInput)) != 0) {
        m_error = "Could not connect JACK MIDI source: " + source;
        return false;
    }
    m_connectedMidiSource = source;
    return true;
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
