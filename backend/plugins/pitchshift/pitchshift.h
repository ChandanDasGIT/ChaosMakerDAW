#pragma once
#include "dsp/pitchvoice.h"
#include "dsp/pedalcontrols.h"
class PitchShift final : public Processor {
public:
    PitchShift();
    std::string typeId() const override { return "pedal.pitchshift"; }
    int latencySamples() const noexcept override { return PitchVoice::Window; }
    void prepare(double sampleRate,int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*,3> controls;
    std::array<Smoother,3> smoothers;
    std::array<PitchVoice,2> voices;
};
