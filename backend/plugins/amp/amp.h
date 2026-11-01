#pragma once
#include "dsp/pedalcontrols.h"
#include "dsp/oversampler4x.h"
class Amp final : public Processor {
public:
    Amp();
    std::string typeId() const override { return "pedal.amp"; }
    int latencySamples() const noexcept override { return 11; } // ~10.5 samples FIR group delay
    void prepare(double sampleRate,int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    struct State {
        float drive=1, dcCoefficient=0.999f;
        std::array<float,2> previous{}, dc{};
    };
    static float shape(float x,void* user) noexcept;
    std::array<Parameter*,6> controls;
    std::array<Smoother,6> smoothers;
    std::array<State,2> state;
    std::array<float,2> inputPrevious{},inputHighpass{},low{},high{},outputLowpass{};
    Oversampler4x oversampler;
    double rate=48000;
    float envelope=0;
};
