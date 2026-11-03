#pragma once
#include "instruments/guitar/dsp/dcblocker.h"
#include "instruments/guitar/dsp/oversampler4x.h"
#include "instruments/guitar/dsp/pedalcontrols.h"

// A lighter, touch-sensitive asymmetric drive; distinct from the metal distortion.
class Crunch final : public Processor {
public:
    Crunch();
    std::string typeId() const override { return "pedal.crunch"; }
    int latencySamples() const noexcept override { return 11; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    struct ShapeState { float drive = 1; DCBlocker dc; };
    static float shape(float input, void* state) noexcept;
    std::array<Parameter*, 4> controls;
    std::array<Smoother, 4> smoothers;
    std::array<ShapeState, 2> shapeState;
    std::array<float, 2> previousInput{}, highpass{}, body{}, tone{};
    Oversampler4x oversampler;
    double rate = 48000;
};
