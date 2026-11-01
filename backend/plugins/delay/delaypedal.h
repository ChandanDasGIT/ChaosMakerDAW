#pragma once

#include "dsp/delayline.h"
#include "processor/processor.h"
#include "processor/smoother.h"

#include <array>

class DelayPedal final : public Processor {
public:
    DelayPedal();
    std::string typeId() const override { return "pedal.delay"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    Parameter& m_timeMs;
    Parameter& m_feedback;
    Parameter& m_mix;
    std::array<DelayLine, 2> m_delayLines;
    std::array<Smoother, 3> m_smoothers;
    double m_sampleRate = 48000.0;
};
