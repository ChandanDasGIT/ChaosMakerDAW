#pragma once
#include "processor/processor.h"
#include "processor/smoother.h"
#include <atomic>
#include <cmath>

// One input trim stage per track (GuitarSuite's input trim), always first in the track's chain.
// The EQ is a separate insert effect placed by the track's effect list (plan 6.1).
class TrackInput final : public Processor {
public:
    std::string typeId() const override { return "track.input"; }
    void prepare(double rate, int frames) override {
        (void)frames;
        m_trim.prepare(rate, 20.0f);
        reset();
    }
    void reset() noexcept override { m_trim.reset(m_gain.load()); }
    void setTrimDb(double db) noexcept { m_gain.store(float(std::pow(10.0, db / 20.0)), std::memory_order_relaxed); }
    void process(ProcessContext& context) noexcept override {
        const float target = m_gain.load(std::memory_order_relaxed);
        for (int i = 0; i < context.audio.frames; ++i) {
            const float gain = m_trim.next(target);
            for (int c = 0; c < context.audio.channelCount; ++c) context.audio.channel(c)[i] *= gain;
        }
    }
private:
    Smoother m_trim;
    std::atomic<float> m_gain{1.0f};
};
