#pragma once

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

// A take's waveform while it is being recorded (restMap: live recording waveform, like
// Reaper's). The take's disk thread appends what it writes; the UI reads a summary at any
// width while recording goes on. Each 256 frames are kept as one (min, max) pair over all
// channels, so an hour at 48 kHz is about 5 MB. Never touched by the audio thread.
class LivePeaks {
public:
    static constexpr int FramesPerPeak = 256;

    // Interleaved samples, `channels` per frame (disk thread).
    void append(const float* interleaved, std::size_t frames, unsigned channels);
    std::int64_t frames() const;
    // `bins` (min, max) pairs covering the part from..to (0..1 of everything so far, the newest
    // frames included), so a long take's visible part is all that is fetched (any thread).
    // Empty for no frames, bins < 1 or an empty range.
    std::vector<std::pair<float, float>> summary(int bins, double from = 0.0, double to = 1.0) const;

private:
    mutable std::mutex m_mutex;
    std::vector<std::pair<float, float>> m_peaks;   // complete groups of FramesPerPeak
    std::pair<float, float> m_partial{0.0f, 0.0f};  // the group being filled
    int m_partialFrames = 0;
    std::int64_t m_frames = 0;
};
