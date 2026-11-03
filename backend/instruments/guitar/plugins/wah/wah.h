#pragma once
#include "instruments/guitar/dsp/pedalcontrols.h"
#include "instruments/guitar/dsp/delayline.h"
class Wah final : public Processor {
public:
    Wah();
    std::string typeId() const override { return "pedal.wah"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*, 3> controls;
    std::array<Smoother, 3> smoothers;
    double rate = 48000;
    std::array<float, 2> ic1{}, ic2{};
};
