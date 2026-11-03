#pragma once
#include "instruments/guitar/dsp/pedalcontrols.h"
#include "instruments/guitar/dsp/delayline.h"
class NoiseGate final : public Processor {
public:
    NoiseGate();
    std::string typeId() const override { return "pedal.noisegate"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*, 3> controls;
    std::array<Smoother, 3> smoothers;
    double rate = 48000;
    float envelope = 0, opening = 0; bool open = false;
};
