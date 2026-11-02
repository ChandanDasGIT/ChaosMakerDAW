#pragma once
#include "core/audioview.h"
#include <algorithm>
#include <atomic>
#include <cmath>

// One audio producer, one UI consumer. Exchange captures even brief peaks
// between UI polls; clipping stays latched until the user clears it.
class PeakMeter {
public:
    void capture(const AudioView& audio) noexcept {
        float peak = 0;
        for (int c = 0; c < audio.channelCount; ++c)
            for (int i = 0; i < audio.frames; ++i)
                peak = std::max(peak, std::abs(audio.channel(c)[i]));
        capturePeak(peak);
    }
    void capturePeak(float peak) noexcept {
        float previous = m_peak.load(std::memory_order_relaxed);
        while (previous < peak && !m_peak.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
        // Include the largest signed PCM capture samples just below 1.0.
        if (peak >= 0.999f) m_clipped.store(true, std::memory_order_relaxed);
    }
    float takePeak() noexcept { return m_peak.exchange(0, std::memory_order_relaxed); }
    bool clipped() const noexcept { return m_clipped.load(std::memory_order_relaxed); }
    void clearClip() noexcept { m_clipped.store(false, std::memory_order_relaxed); }
private:
    static_assert(std::atomic<float>::is_always_lock_free);
    std::atomic<float> m_peak{0};
    std::atomic<bool> m_clipped{false};
};
