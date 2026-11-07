#pragma once

#include "audiodevice/audiodevice.h"
#include "midi/midievent.h"

#include <jack/jack.h>
#include <jack/midiport.h>

#include <array>
#include <atomic>
#include <string>
#include <vector>

class JackAudioDevice final : public AudioDevice {
public:
    JackAudioDevice() = default;
    ~JackAudioDevice() override;

    bool open(AudioCallback* callback, int wantedBufferFrames) override;
    void close() override;
    double sampleRate() const override;
    DeviceLatency latency() const override;
    bool setBufferSize(int frames) override;
    std::vector<std::string> inputNames() const override;
    std::vector<std::string> outputNames() const override;
    bool connectInput(const std::string& source) override;
    int connectInputChannel(const std::string& source) override;
    int inputChannelCapacity() const override { return inputChannels-1; }
    bool connectOutput(const std::string& left, const std::string& right) override;
    std::vector<std::string> midiInputNames() const override { return m_midiInputNames; }
    // Lists JACK's MIDI sources again; with none connected yet (and "No MIDI input" not
    // chosen), connects the first one.
    void refreshMidiInputs() override;
    bool connectMidiInput(const std::string& source) override;
    std::string connectedMidiInput() const override { return m_connectedMidiSource; }
    std::string lastError() const override { return m_error; }

private:
    static int process(jack_nframes_t frames, void* context) noexcept;
    static int bufferSizeChanged(jack_nframes_t frames, void* context) noexcept;
    static int xrun(void* context) noexcept;
    static void shutdown(void* context) noexcept;
    int processBlock(jack_nframes_t frames) noexcept;
    void enumeratePorts();
    bool disconnectAll(jack_port_t* port, bool portIsSource);

    jack_client_t* m_client = nullptr;
    static constexpr int inputChannels = 32;
    std::array<jack_port_t*, inputChannels> m_inputs{};
    std::array<std::string, inputChannels> m_sources{};
    jack_port_t* m_midiInput = nullptr;
    MidiBuffer m_midiInBuffer;
    std::array<jack_port_t*, 2> m_outputs{};
    std::array<float*, inputChannels> m_inputBuffers{};
    std::array<float*, 2> m_outputBuffers{};
    AudioCallback* m_callback = nullptr;
    std::atomic<int> m_bufferFrames{0};
    double m_sampleRateHz = 0.0;
    int m_requestedBufferFrames = 0;
    bool m_bufferRequestAccepted = false;
    std::vector<std::string> m_inputNames;
    std::vector<std::string> m_outputNames;
    std::vector<std::string> m_midiInputNames;
    std::string m_connectedMidiSource;
    bool m_midiOffByUser = false;          // "No MIDI input" chosen: do not connect one by itself
    std::string m_error;
};
