#pragma once

#include "instruments/guitar/dsp/delayline.h"
#include "processor/processor.h"
#include "processor/smoother.h"

#include <array>
#include <vector>

class ReverbPedal final : public Processor {
public:
    ReverbPedal();
    std::string typeId() const override { return "pedal.reverb"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    struct Comb { DelayLine line; float dampingState = 0.0f; };
    Parameter& m_room;
    Parameter& m_damping;
    Parameter& m_mix;
    std::array<std::array<Comb, 4>, 2> m_combs;
    std::array<std::array<DelayLine, 2>, 2> m_allpasses;
    std::array<Smoother, 3> m_smoothers;
    double m_sampleRate = 48000.0;
};
