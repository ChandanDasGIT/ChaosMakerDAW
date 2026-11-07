#pragma once
#include "dsp/biquad.h"
#include "dsp/eqbanddesign.h"
#include "instruments/guitar/dsp/pedalcontrols.h"
#include "instruments/guitar/dsp/oversampler4x.h"

// Guitar amp (rebuilt in plan 6.3). Three channels share one signal path:
//
//   input low cut (30 Hz) -> channel voicing (low cut, mid push, treble boost) -> preamp
//   -> tone stack (its own mid scoop, then bass, mid, treble) -> presence + resonance
//   -> power stage -> master
//
// Preamp: 2 stages (3 on High Gain) of uneven, tube-like clipping at 4x the sample rate, with
// coupling and treble-loss filters between stages. Hard picking shifts each stage's bias
// (grid conduction), so the sound reacts to playing and recovers within tens of ms.
// Power stage: soft push-pull clipping with supply sag (dig in: it compresses and blooms).
// Filters refresh every 16 samples on a fixed grid, so the block size never changes the result.
class Amp final : public Processor {
public:
    enum Channel { Clean = 0, Crunch = 1, HighGain = 2 };

    Amp();
    std::string typeId() const override { return "pedal.amp"; }
    int latencySamples() const noexcept override { return 22; } // two 4x oversampling passes
    void prepare(double sampleRate,int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    static constexpr int ParameterCount = 8;  // drive bass mid treble presence master channel resonance
    static constexpr int RefreshFrames = 16;

    // A linear filter (up to four biquads) with its own state per channel.
    struct Filter {
        eqband::Design design;
        std::array<std::array<biquad::State, eqband::MaxSections>, 2> state{};
        bool active = false;
        float run(int channel, float x) noexcept;
        void set(const eqband::Settings& settings, double rate) noexcept;
    };

    // Preamp state for one channel, used inside the oversampler (4x rate).
    struct Preamp {
        int stages = 2;
        float stageGain = 1;
        float couplingCoefficient = 0.999f, trebleCoefficient = 0.5f;
        float biasAttack = 0.01f, biasRelease = 0.001f;
        std::array<float, 3> bias{}, couplingIn{}, couplingOut{}, treble{};
    };
    // Power stage state for one channel (4x rate).
    struct Power {
        float drive = 1, sag = 0.2f;
        float envelope = 0, attack = 0.01f, release = 0.001f;
    };

    static float preamp(float x, void* user) noexcept;
    static float power(float x, void* user) noexcept;
    void refresh(const std::array<float, ParameterCount>& values) noexcept;

    std::array<Parameter*, ParameterCount> controls;
    std::array<Smoother, ParameterCount> smoothers;
    std::array<float, ParameterCount> applied{};   // values the filters were last built from
    int appliedChannel = -1;
    Filter inputCut, tight, push, bright, scoop, bass, mid, treble, presence, resonance, outputCut;
    std::array<Preamp, 2> preamps;
    std::array<Power, 2> powers;
    Oversampler4x preampOversampler, powerOversampler;
    float outputTrim = 1;
    long long frameCounter = 0;
    double rate = 48000;
};
