#pragma once
#include "instruments/guitar/dsp/drivestage.h"
#include "instruments/guitar/dsp/oversampler4x.h"
#include "instruments/guitar/dsp/pedalcontrols.h"
#include "instruments/guitar/dsp/pedalfilter.h"

// A lighter, touch-sensitive asymmetric drive; distinct from the metal distortion.
// Reworked in plan 6.3: a light low cut and presence push before one uneven, bias-shifting
// stage (it reacts to picking like a small amp), then a fixed low-mid scoop, Body (low
// shelf), Tone (treble shelf)
// and a gentle top-end roll-off. Level is a pure output gain.
class Crunch final : public Processor {
public:
    Crunch();
    std::string typeId() const override { return "pedal.crunch"; }
    int latencySamples() const noexcept override { return 11; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    static constexpr int RefreshFrames = 16;
    struct ShapeState { float drive = 1; DriveStage stage; };
    static float shape(float input, void* state) noexcept;
    void refresh(float tone, float body) noexcept;
    std::array<Parameter*, 4> controls;
    std::array<Smoother, 4> smoothers;
    std::array<ShapeState, 2> shapeState;
    PedalFilter lowCut, presence, scoop, bodyShelf, toneShelf, rolloff;
    Oversampler4x oversampler;
    float appliedTone = -1, appliedBody = -100;
    long long frameCounter = 0;
    double rate = 48000;
};
