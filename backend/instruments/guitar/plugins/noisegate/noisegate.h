#pragma once
#include "instruments/guitar/dsp/pedalcontrols.h"
#include "instruments/guitar/dsp/delayline.h"
// Noise gate: opens when the input passes Threshold, closes 6 dB below it (hysteresis) once a
// 50 ms hold has run out, so a brief dip inside a phrase does not punch a hole in the note
// (plan 6.3 step 4). Attack and Release set the fade in and out.
class NoiseGate final : public Processor {
public:
    NoiseGate();
    std::string typeId() const override { return "pedal.noisegate"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*, 3> controls;
    std::array<Smoother, 3> smoothers;
    double rate = 48000;
    static constexpr double HoldSeconds = 0.05;
    float envelope = 0, opening = 0; bool open = false;
    int holdLeft = 0, holdFrames = 2400;
};
