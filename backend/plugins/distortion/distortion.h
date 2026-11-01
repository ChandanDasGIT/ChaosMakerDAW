#pragma once
#include "dsp/dcblocker.h"
#include "dsp/oversampler4x.h"
#include "dsp/pedalcontrols.h"

// Latency of the 4x oversampler path, in samples at the host rate.
// Verified by distortion_latency_test.cpp: re-run it whenever the oversampler filters change.
constexpr int kDistortionLatencySamples = 11;

// Tight high-gain clipping with post-distortion EQ and a sweepable mid band.
class Distortion final : public Processor {
public:
    Distortion();
    std::string typeId() const override { return "pedal.distortion"; }
    int latencySamples() const noexcept override { return kDistortionLatencySamples; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    struct ShapeState { float drive = 1; DCBlocker dc; };
    static float shape(float input, void* state) noexcept;
    void updateCoefficients(const std::array<float, 7>& values) noexcept;

    // Derived coefficients (tan/exp/pow) are refreshed every kControlInterval samples.
    // The 20 ms parameter smoothers hide the steps.
    static constexpr int kControlInterval = 16;
    // Small input bias gives asymmetric clipping (even harmonics) without a hard corner.
    static constexpr float kBias = 0.2f;

    std::array<Parameter*, 7> controls;
    std::array<Smoother, 7> smoothers;
    std::array<ShapeState, 2> shapeState;
    std::array<float, 2> previousInput{}, highpass{}, low{}, high{}, mid1{}, mid2{}, outputLowpass{};
    Oversampler4x oversampler;
    double rate = 48000;

    // Fixed per sample rate (set in prepare).
    float lowCoefficient = 0, highCoefficient = 0, outputCoefficient = 0;
    // Cached control-rate values.
    float drive = 1, highpassPole = 0, bass = 1, mids = 1, treble = 1, level = 1, g = 0, a = 1;
    int controlCounter = 0;
};
