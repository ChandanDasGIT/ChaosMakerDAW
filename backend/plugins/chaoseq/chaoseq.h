#pragma once

#include "dsp/biquad.h"
#include "plugins/chaoseq/chaoseqsettings.h"
#include "processor/processor.h"
#include "rt/rtaudioring.h"

#include <array>
#include <atomic>
#include <span>
#include <string>

// Chaos EQ (plan 6.2): up to 24 parametric bands, each with its own shape, slope, bypass,
// solo and stereo placement, plus output gain and auto-gain.
//
// Settings arrive through setSettings() from any thread (they are stored in lock-free
// parameters). The audio thread never jumps to a new value:
//   - frequency, gain and Q glide (about 20 ms);
//   - turning a band on/off, bypass, and changing its shape, slope or placement fade the
//     band's effect out and back in (about 10 ms each way);
//   - solo crossfades between the full EQ and the soloed band's region;
//   - output gain glides.
// For the spectrum display, the output (mixed to mono) is copied into a lock-free ring while
// someone listens (setAnalyzerListening); readAnalyzer() takes it out on another thread.
class ChaosEq final : public Processor {
public:
    static constexpr const char* TypeId = "eq.chaos";

    ChaosEq();
    std::string typeId() const override { return TypeId; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    // Any thread. The settings are sanitised first.
    void setSettings(const chaoseq::Settings& settings) noexcept;
    chaoseq::Settings settings() const noexcept;

    // Analyzer feed. Listening on: process() copies its output into the ring (dropped when
    // full). One reader thread at a time; returns how many samples it moved into out.
    void setAnalyzerListening(bool listening) noexcept { m_listening.store(listening, std::memory_order_relaxed); }
    bool analyzerListening() const noexcept { return m_listening.load(std::memory_order_relaxed); }
    std::size_t readAnalyzer(std::span<float> out) noexcept;

private:
    static constexpr int SubBlock = 32;   // coefficients are refreshed at most this often

    struct BandParameters {
        Parameter* inUse = nullptr;
        Parameter* bypassed = nullptr;
        Parameter* solo = nullptr;
        Parameter* placement = nullptr;
        Parameter* shape = nullptr;
        Parameter* frequency = nullptr;
        Parameter* gain = nullptr;
        Parameter* q = nullptr;
        Parameter* slope = nullptr;
    };

    // What a filter should become; read from the parameters once per block.
    struct Target {
        bool on = false;
        int source = 0;   // band index; the solo voice re-fades when it follows another band
        int shape = 0, slope = 0, placement = 0;
        double frequency = 1000.0, gain = 0.0, q = eqband::DefaultQ;
    };

    // One running filter (a band, or the solo band pass) with its fade and glide state.
    struct Voice {
        int source = -1, shape = -1, slope = -1, placement = -1;   // current structure; -1 = never set
        double frequency = 1000.0, gain = 0.0, q = eqband::DefaultQ;
        double mix = 0.0;                             // 0 = no effect, 1 = full effect
        eqband::Design design;
        std::array<std::array<biquad::State, eqband::MaxSections>, 2> state{};
    };

    struct Ramp {
        double from = 0.0, to = 0.0;
        bool silent() const noexcept { return from <= 0.0 && to <= 0.0; }
    };

    Target bandTarget(int band) const noexcept;
    Ramp advance(Voice& voice, const Target& target, int frames) noexcept;
    void runVoice(Voice& voice, Ramp mix, double (*buffer)[SubBlock], int channels, int frames) noexcept;

    std::array<BandParameters, chaoseq::MaxBands> m_bands{};
    Parameter* m_output = nullptr;
    Parameter* m_autoGain = nullptr;
    Parameter* m_autoGainDb = nullptr;

    std::array<Voice, chaoseq::MaxBands> m_voices{};
    Voice m_soloVoice;
    RtAudioRing<16384> m_analyzer;
    std::atomic<bool> m_listening{false};
    double m_outputGain = 1.0;      // current linear output gain
    double m_sampleRate = 48000.0;
};
