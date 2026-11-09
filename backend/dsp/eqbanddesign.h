#pragma once

#include "dsp/biquad.h"

#include <array>

// The maths for one parametric EQ band (Chaos EQ, guitar amp): settings in, filter sections out.
// A band is a chain of up to four biquads; steeper slopes use more of them.
namespace eqband {

enum class Shape { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf };

// Steepness of cuts and shelves. Other shapes ignore it.
enum class Slope { Db6, Db12, Db24, Db48 };

constexpr double MinFrequencyHz = 10.0;
constexpr double MaxFrequencyHz = 30000.0;
constexpr double MinGainDb = -30.0;
constexpr double MaxGainDb = 30.0;
constexpr double MinQ = 0.025;
constexpr double MaxQ = 40.0;
constexpr double DefaultQ = 0.70710678118654752440;   // Butterworth: flat, no bump at the corner
constexpr int MaxSections = 4;

struct Settings {
    Shape shape = Shape::Bell;
    double frequencyHz = 1000.0;
    double gainDb = 0.0;     // bell, shelves and tilt only
    double q = DefaultQ;     // width of bell/notch/band pass; corner resonance of cuts and shelves
    Slope slope = Slope::Db12;
};

// How a bell is made. Cookbook: the RBJ cookbook bell, whose gain always returns to 0 dB at
// Nyquist, so a bell high up (above ~10 kHz at 44.1/48 kHz) comes out narrower than its Q (the
// guitar pedals and the harmonium were voiced with it). Matched: the gain at Nyquist follows the
// analog bell (Orfanidis, "Digital parametric equalizer design with prescribed Nyquist-frequency
// gain", 1997), so a high bell keeps its width; low bells are the same as the cookbook's.
enum class Bells { Cookbook, Matched };

struct Design {
    std::array<biquad::Coefficients, MaxSections> sections{};
    int sectionCount = 0;
};

bool usesGain(Shape shape) noexcept;
bool usesSlope(Shape shape) noexcept;
int dbPerOctave(Slope slope) noexcept;

// Brings every value into range. Non-finite values fall back to the defaults. The frequency
// also stays below Nyquist for this sample rate.
Settings sanitize(const Settings& settings, double sampleRate) noexcept;

// Filter sections for the band. Real-time safe: no allocation, no locks.
Design design(const Settings& settings, double sampleRate, Bells bells = Bells::Cookbook) noexcept;

// Response of the whole band at one frequency, as a gain factor and in dB.
double magnitude(const Design& design, double frequencyHz, double sampleRate) noexcept;
double magnitudeDb(const Design& design, double frequencyHz, double sampleRate) noexcept;

} // namespace eqband
