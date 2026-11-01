#pragma once

#include "audiodevice/audiodevice.h"

#include <jack/jack.h>

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
    bool connectOutput(const std::string& left, const std::string& right) override;
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
    jack_port_t* m_input = nullptr;
    std::array<jack_port_t*, 2> m_outputs{};
    std::array<float*, 1> m_inputBuffers{};
    std::array<float*, 2> m_outputBuffers{};
    AudioCallback* m_callback = nullptr;
    std::atomic<int> m_bufferFrames{0};
    double m_sampleRateHz = 0.0;
    int m_requestedBufferFrames = 0;
    bool m_bufferRequestAccepted = false;
    std::vector<std::string> m_inputNames;
    std::vector<std::string> m_outputNames;
    std::string m_error;
};
