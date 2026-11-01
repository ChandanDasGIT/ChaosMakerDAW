#pragma once
#include "dsp/pedalcontrols.h"
#include "dsp/delayline.h"
class Compressor final : public Processor {
public:
    Compressor();
    std::string typeId() const override { return "pedal.compressor"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*, 5> controls;
    std::array<Smoother, 5> smoothers;
    double rate = 48000;
    float reduction = 0;
};
