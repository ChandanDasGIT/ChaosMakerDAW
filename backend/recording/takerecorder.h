#pragma once
#include "core/audiodata.h"
#include "rt/rtaudioring.h"
#include <atomic>
#include <fstream>
#include <memory>
#include <thread>

// UI owns the object until the audio thread has released it and the worker is done.
// One producer (audio), one consumer (disk). A full ring ends the take, never skips time.
class TakeRecorder {
public:
    TakeRecorder(const std::string& path, double sampleRate);
    ~TakeRecorder();
    void begin(std::int64_t position) noexcept { startSample = position; }
    bool capture(const float* input, int frames) noexcept;
    void finish() noexcept { inputFinished.store(true, std::memory_order_release); }
    bool done() const noexcept { return completed.load(std::memory_order_acquire); }
    void wait(); // only after the audio thread has released this object
    std::shared_ptr<const AudioData> audio() const { return result; } // only after done()/wait()
    const std::string& error() const { return failure; } // only after done()/wait()
    std::int64_t startSample = 0; // read only after done()
private:
    void run();
    RtAudioRing<1 << 18> ring; // ~5.5 seconds at 48 kHz
    std::atomic<bool> inputFinished{false}, completed{false}, overflow{false};
    std::ofstream file;
    std::thread worker;
    std::shared_ptr<AudioData> result;
    std::string failure;
};
// Reads our mono float32 take WAVs; validates the entire file before publishing audio.
std::shared_ptr<const AudioData> loadTakeWav(const std::string& path);
