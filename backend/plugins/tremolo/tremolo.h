#pragma once
#include "dsp/pedalcontrols.h"
#include "dsp/delayline.h"
class Tremolo final : public Processor {
public:
    Tremolo();
    std::string typeId() const override { return "pedal.tremolo"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*, 8> controls;
    std::array<Smoother, 2> smoothers;
    Smoother pulseRate;
    bool initialiseRate = true;
    double rate = 48000;
    double phase = 0;
    float fastEnvelope = 0, slowEnvelope = 0, sustainMix = 0;
    float fastAttack = 0, fastRelease = 0, slowCoefficient = 0, blendCoefficient = 0;
    std::uint64_t noteAge = 0;
    bool noteActive = false, attackLatched = false;
};
