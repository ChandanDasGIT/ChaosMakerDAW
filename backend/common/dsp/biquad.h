#pragma once

// One second-order filter section (a "biquad"), the building block of EQ bands.
// Coefficients are normalised so a0 = 1. Everything is double precision: a narrow band at
// 20 Hz has poles very close to 1, and float rounding there audibly moves the filter.
namespace biquad {

struct Coefficients {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;   // numerator (feed-forward)
    double a1 = 0.0, a2 = 0.0;             // denominator (feedback), a0 is 1
};

// Memory of one section on one channel (transposed direct form II).
struct State {
    double z1 = 0.0, z2 = 0.0;
};

// Filters one sample. Real-time safe: no allocation, no locks.
inline double process(const Coefficients& c, State& s, double in) noexcept
{
    const double out = c.b0 * in + s.z1;
    s.z1 = c.b1 * in - c.a1 * out + s.z2;
    s.z2 = c.b2 * in - c.a2 * out;
    return out;
}

// How much the section multiplies a sine at frequencyHz (1 = unchanged).
double magnitude(const Coefficients& c, double frequencyHz, double sampleRate) noexcept;

} // namespace biquad
