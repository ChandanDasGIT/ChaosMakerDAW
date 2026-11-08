#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// The electronic drum kit's sounds, synthesised: no samples. Every drum is a recipe of up to two
// pitched tones (a sine with a falling pitch), up to two filtered noise layers (a clap's noise
// comes in bursts) and a metallic layer (six square waves at inharmonic ratios, as cymbals and
// hi-hats are made on analogue drum machines). Three styles: 808 (deep, long kick, soft hats),
// 909 (punchy kick with a click, sharper snare, bright hats) and Studio (closer to an acoustic
// kit: a solid kick with shell body and beater, toms with an overtone, noisier, more natural
// metal). One Voice plays one hit.
namespace electrodrums {

enum class Style { Style808, Style909, Studio };

enum class Drum {
    Kick, SideStick, Snare, Clap, TomLow, TomMid, TomHigh,
    HatClosed, HatPedal, HatOpen, Crash, Ride, Count
};
constexpr int DrumCount = int(Drum::Count);

std::string_view drumId(Drum drum) noexcept;       // "kick", "hihatOpen" ... as drummap ids
std::string_view styleId(Style style) noexcept;    // "808", "909", "studio"

// The per-drum knobs (the level is applied by the caller as a gain).
struct Knobs {
    double tuneSemitones = 0.0;           // MinTune .. MaxTune
    double decay = 1.0;                   // times the recipe's decay, MinDecay .. MaxDecay
};
constexpr double MinTune = -12.0, MaxTune = 12.0;
constexpr double MinDecay = 0.25, MaxDecay = 4.0;

enum class FilterMode { LowPass, BandPass, HighPass };

struct Tone {
    double frequency = 0.0;               // Hz at the end of the sweep; 0 = layer off
    double sweep = 1.0;                   // starts at frequency * sweep, falls to frequency
    double sweepSeconds = 0.0;            // time constant of the fall
    double decaySeconds = 0.0;            // to -80 dB
    double gain = 0.0;
};

struct Noise {
    FilterMode mode = FilterMode::HighPass;
    double frequency = 1000.0, q = 0.707;
    double decaySeconds = 0.0;            // 0 = layer off
    double gain = 0.0;
    int bursts = 1;                       // a clap: bursts - 1 short bursts before the decay
    double burstSpacingSeconds = 0.0;
    double burstDecaySeconds = 0.0;
};

struct Metal {
    std::array<double, 6> frequencies{};  // square waves; all 0 = layer off
    double bandPass = 8000.0, bandQ = 1.0;
    double highPass = 6000.0;
    double noise = 0.0;                   // white noise mixed in before the filters, 0..1
    double decaySeconds = 0.0;
    double gain = 0.0;
};

struct Recipe {
    std::array<Tone, 2> tones{};
    std::array<Noise, 2> noises{};
    Metal metal;
    double drive = 1.0;                   // > 1: soft saturation (tanh), same peak level
    double output = 0.8;                  // after the saturation: headroom for a whole kit
    double lengthSeconds() const noexcept; // when the longest layer reaches -80 dB
};

// The recipe for one drum in one style with the knobs applied: tune moves every frequency
// (pitch and filters), decay stretches every decay time.
Recipe recipe(Drum drum, Style style, const Knobs& knobs) noexcept;

// One drum hit. Real-time safe: no allocation; start() only sets numbers.
class Voice {
public:
    // gain: velocity and level, linear. seed: the noise sequence (same seed, same sound).
    void start(Drum drum, Style style, const Knobs& knobs, float gain, double sampleRate,
               std::uint32_t seed) noexcept;
    void choke() noexcept;                // fades out in ChokeSeconds (open hat cut by closed)
    // Adds `frames` samples of the hit to mono.
    void render(float* mono, int frames) noexcept;

    bool active() const noexcept { return m_active; }
    bool choking() const noexcept { return m_chokeLeft >= 0; }
    Drum drum() const noexcept { return m_drum; }

    static constexpr double ChokeSeconds = 0.005;

private:
    struct Svf {
        double a1 = 0, a2 = 0, a3 = 0, k = 0, ic1 = 0, ic2 = 0;
        void set(double frequency, double q, double sampleRate) noexcept;
        double process(double in, FilterMode mode) noexcept;
    };
    struct ToneState { double phase = 0, sweep = 0, sweepCoef = 0, amp = 0, ampCoef = 0; };
    struct NoiseState {
        Svf filter;
        double amp = 0, ampCoef = 0, burstCoef = 0;
        int burstsLeft = 0;                            // short bursts still to come (a clap)
        long burstSpacing = 1, untilNext = 0;          // samples between bursts, to the next
    };
    double noise() noexcept;              // white, -1 .. 1

    Recipe m_recipe;
    Drum m_drum = Drum::Kick;
    double m_rate = 48000.0;
    float m_gain = 0.0f;
    bool m_active = false;
    long m_samplesLeft = 0;
    long m_chokeLeft = -1, m_chokeSamples = 1;
    std::uint32_t m_seed = 1;
    std::array<ToneState, 2> m_tones{};
    std::array<NoiseState, 2> m_noises{};
    std::array<double, 6> m_metalPhase{};
    Svf m_metalBand, m_metalHigh;
    double m_metalAmp = 0, m_metalCoef = 0;
    double m_driveNorm = 1.0;
};

} // namespace electrodrums
