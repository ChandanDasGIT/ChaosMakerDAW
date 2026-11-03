#pragma once
#include "instruments/guitar/dsp/pedalcontrols.h"

// Three movable peaking bands plus output level; can be placed after amp/cab.
class ParametricEq final : public Processor {
public:
    ParametricEq();
    std::string typeId() const override { return "pedal.parametriceq"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*, 10> controls;
    std::array<Smoother, 10> smoothers;
    struct State { float ic1 = 0, ic2 = 0; };
    std::array<std::array<State, 2>, 3> state{};
    double rate = 48000;
};
