#pragma once
#include "instruments/guitar/dsp/pedalcontrols.h"
#include "instruments/guitar/dsp/delayline.h"
class Chorus final : public Processor {
public:
    Chorus();
    std::string typeId() const override { return "pedal.chorus"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*, 3> controls;
    std::array<Smoother, 3> smoothers;
    double rate = 48000;
    std::array<DelayLine, 2> lines; double phase = 0;
};
