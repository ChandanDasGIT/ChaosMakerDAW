#pragma once
#include "core/audioview.h"
#include "midi/midievent.h"

#include <cstdint>
#include <string>
#include <vector>

struct AudioIO {
    AudioView input;
    AudioView output;
    const MidiBuffer* midiIn = nullptr;
    int frames = 0;                                       // this block. Do not assume it stays the same
};

class AudioCallback {
public:
    virtual ~AudioCallback() = default;
    virtual void process(const AudioIO& io) noexcept = 0;           // audio thread
    virtual void deviceChanged(double sampleRate, int maxFrames) = 0;
    virtual void xrunOccurred() noexcept = 0;
};

struct DeviceLatency {
    int captureFrames = 0;
    int playbackFrames = 0;
    int requestedBufferFrames = 0;
};

class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    virtual bool open(AudioCallback* callback, int wantedBufferFrames) = 0;
    virtual void close() = 0;
    virtual double sampleRate() const = 0;
    virtual DeviceLatency latency() const = 0;
    virtual bool setBufferSize(int frames) = 0;
    virtual std::vector<std::string> inputNames() const = 0;
    virtual std::vector<std::string> outputNames() const = 0;
    virtual bool connectInput(const std::string& source) = 0;
    // Return a stable callback channel for this source, or -1 on failure.
    virtual int connectInputChannel(const std::string& source) { return connectInput(source) ? 0 : -1; }
    virtual int inputChannelCapacity() const { return 1; }
    virtual bool connectOutput(const std::string& left, const std::string& right) = 0;
    virtual std::string lastError() const = 0;
};
