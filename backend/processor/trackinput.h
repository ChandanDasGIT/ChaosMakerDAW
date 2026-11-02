#pragma once
#include "plugins/eq/tenbandeq.h"
#include "processor/smoother.h"
#include <atomic>
#include <cmath>

// One input stage per track: even filter history and trim smoothing are isolated.
class TrackInput final : public Processor {
public:
    std::string typeId() const override { return "track.input"; }
    void prepare(double rate, int frames) override {
        m_eq.prepare(rate, frames);
        m_trim.prepare(rate, 20.0f);
        reset();
    }
    void reset() noexcept override { m_eq.reset(); m_trim.reset(m_gain.load()); }
    void setTrimDb(double db) noexcept { m_gain.store(float(std::pow(10.0, db / 20.0)), std::memory_order_relaxed); }
    void setEqBand(int band, float db) noexcept { m_eq.setBandGain(band, db); }
    void process(ProcessContext& context) noexcept override {
        const float target = m_gain.load(std::memory_order_relaxed);
        for (int i = 0; i < context.audio.frames; ++i) {
            const float gain = m_trim.next(target);
            for (int c = 0; c < context.audio.channelCount; ++c) context.audio.channel(c)[i] *= gain;
        }
        m_eq.process(context);
    }
private:
    TenBandEq m_eq;
    Smoother m_trim;
    std::atomic<float> m_gain{1.0f};
};
