#pragma once
#include "instruments/guitar/dsp/drivestage.h"
#include "instruments/guitar/dsp/oversampler4x.h"
#include "instruments/guitar/dsp/pedalcontrols.h"
#include "instruments/guitar/dsp/pedalfilter.h"

// Fuzz (reworked in plan 6.3). A starved, very uneven first stage whose bias jumps on hard
// picking (it sputters and cleans up with the guitar's volume), then a Sustain stage, both in
// one 4x pass. Tone blends a dark and a bright path, with the classic scooped middle at noon.
// Level is a pure output gain.
class Fuzz final : public Processor {
public:
    Fuzz();
    std::string typeId() const override { return "pedal.fuzz"; }
    int latencySamples() const noexcept override { return 27; }   // one 4x pass: 26.5 (oversampler test)
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    struct ShapeState { float drive = 1, sustain = 1; DriveStage first, second; };
    static float shape(float input, void* state) noexcept;
    std::array<Parameter*, 4> controls;
    std::array<Smoother, 4> smoothers;
    std::array<ShapeState, 2> shapeState;
    PedalFilter lowCut, dark, bright, rolloff;
    Oversampler4x oversampler;
    double rate = 48000;
};
