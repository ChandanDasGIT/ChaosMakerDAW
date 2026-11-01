#pragma once
#include "dsp/dcblocker.h"
#include "dsp/oversampler4x.h"
#include "dsp/pedalcontrols.h"

class Fuzz final : public Processor {
public:
    Fuzz();
    std::string typeId() const override { return "pedal.fuzz"; }
    int latencySamples() const noexcept override { return 11; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    struct ShapeState { float drive = 1, sustain = 1; DCBlocker dc; };
    static float shape(float input, void* state) noexcept;
    std::array<Parameter*, 4> controls;
    std::array<Smoother, 4> smoothers;
    std::array<ShapeState, 2> shapeState;
    std::array<float, 2> previousInput{}, highpass{}, tone1{}, tone2{};
    Oversampler4x oversampler;
    double rate = 48000;
};
