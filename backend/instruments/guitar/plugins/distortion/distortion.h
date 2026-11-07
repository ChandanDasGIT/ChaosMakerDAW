#pragma once
#include "instruments/guitar/dsp/drivestage.h"
#include "instruments/guitar/dsp/oversampler4x.h"
#include "instruments/guitar/dsp/pedalcontrols.h"
#include "instruments/guitar/dsp/pedalfilter.h"

// Latency of the single 4x oversampler pass, in samples at the host rate: 26.5, rounded up
// (measured by the oversampler test; crunch and fuzz report the same).
constexpr int kDistortionLatencySamples = 27;

// Tight high-gain distortion (reworked in plan 6.3). Tight is a steep low cut before clipping
// (70-400 Hz), plus a fixed upper-mid push for bite. Two clipping stages run in the same 4x
// pass with treble loss between them, which is smoother than one hard stage. After clipping:
// a fixed low-mid scoop (clipping regenerates the mud the low cut removed), bass and treble
// shelves, a peaking Mid band at Mid frequency (exact gain), a top-end
// roll-off and Level (a pure output gain).
class Distortion final : public Processor {
public:
    Distortion();
    std::string typeId() const override { return "pedal.distortion"; }
    int latencySamples() const noexcept override { return kDistortionLatencySamples; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    static constexpr int RefreshFrames = 16;
    struct ShapeState {
        float stageGain = 1, trebleCoefficient = 0.5f, treble = 0;
        DriveStage first, second;
    };
    static float shape(float input, void* state) noexcept;
    void refresh(const std::array<float, 7>& values) noexcept;
    std::array<Parameter*, 7> controls;
    std::array<Smoother, 7> smoothers;
    std::array<ShapeState, 2> shapeState;
    PedalFilter tight, bite, scoop, bass, mid, treble, rolloff;
    std::array<float, 7> applied{};
    Oversampler4x oversampler;
    long long frameCounter = 0;
    double rate = 48000;
};
