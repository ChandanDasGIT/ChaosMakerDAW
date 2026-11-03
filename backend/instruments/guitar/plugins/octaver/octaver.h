#pragma once
#include "instruments/guitar/dsp/pitchvoice.h"
#include "instruments/guitar/dsp/pedalcontrols.h"
class Octaver final : public Processor {
public:
    Octaver();
    std::string typeId() const override { return "pedal.octaver"; }
    int latencySamples() const noexcept override { return PitchVoice::Window; }
    void prepare(double sampleRate,int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*,4> controls;
    std::array<Smoother,4> smoothers;
    std::array<PitchVoice,2> down,up;
};
