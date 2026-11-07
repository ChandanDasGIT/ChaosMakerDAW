#include "eqbanddesign.h"

#include <algorithm>
#include <cmath>

// Formulas: second-order sections follow the RBJ "Audio EQ Cookbook". First-order sections
// (6 dB/oct) use the bilinear transform with the corner pre-warped, so the corner lands
// exactly on the chosen frequency. Steeper slopes chain second-order sections whose Q values
// come from the Butterworth pole angles; for shelves each section carries an equal share of
// the gain (Holters & Zoelzer). At DefaultQ that gives a flat cut (-3 dB at the corner) and a
// shelf with no overshoot; a higher Q adds a resonant bump at the corner.

namespace eqband {
namespace {

constexpr double pi = 3.14159265358979323846;

int orderOf(Slope slope) noexcept
{
    switch (slope) {
    case Slope::Db6:  return 1;
    case Slope::Db12: return 2;
    case Slope::Db24: return 4;
    case Slope::Db48: return 8;
    }
    return 2;
}

// Q of section k (0 = most resonant) of a Butterworth filter of the given even order.
// The user's Q scales only the most resonant section, so at DefaultQ the result is exact
// Butterworth and a 12 dB/oct band uses the user's Q as it is.
double sectionQ(int order, int k, double userQ) noexcept
{
    const double butterworth = 1.0 / (2.0 * std::sin((2.0 * k + 1.0) * pi / (2.0 * order)));
    return k == 0 ? butterworth * (userQ / DefaultQ) : butterworth;
}

biquad::Coefficients normalised(double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

struct Angle {
    double cosw, alpha;
};

Angle angle(double frequencyHz, double q, double sampleRate) noexcept
{
    const double w0 = 2.0 * pi * frequencyHz / sampleRate;
    return {std::cos(w0), std::sin(w0) / (2.0 * q)};
}

// Pre-warped analog corner for first-order sections.
double warped(double frequencyHz, double sampleRate) noexcept
{
    return std::tan(pi * frequencyHz / sampleRate);
}

biquad::Coefficients bell(double f, double gainDb, double q, double sr) noexcept
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const auto [cosw, alpha] = angle(f, q, sr);
    return normalised(1.0 + alpha * a, -2.0 * cosw, 1.0 - alpha * a,
                      1.0 + alpha / a, -2.0 * cosw, 1.0 - alpha / a);
}

biquad::Coefficients lowCut2(double f, double q, double sr) noexcept
{
    const auto [cosw, alpha] = angle(f, q, sr);
    return normalised((1.0 + cosw) / 2.0, -(1.0 + cosw), (1.0 + cosw) / 2.0,
                      1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

biquad::Coefficients highCut2(double f, double q, double sr) noexcept
{
    const auto [cosw, alpha] = angle(f, q, sr);
    return normalised((1.0 - cosw) / 2.0, 1.0 - cosw, (1.0 - cosw) / 2.0,
                      1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

biquad::Coefficients lowCut1(double f, double sr) noexcept
{
    const double k = warped(f, sr);
    return normalised(1.0, -1.0, 0.0, 1.0 + k, k - 1.0, 0.0);
}

biquad::Coefficients highCut1(double f, double sr) noexcept
{
    const double k = warped(f, sr);
    return normalised(k, k, 0.0, 1.0 + k, k - 1.0, 0.0);
}

biquad::Coefficients lowShelf2(double f, double gainDb, double q, double sr) noexcept
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const auto [cosw, alpha] = angle(f, q, sr);
    const double s = 2.0 * std::sqrt(a) * alpha;
    return normalised(a * ((a + 1.0) - (a - 1.0) * cosw + s),
                      2.0 * a * ((a - 1.0) - (a + 1.0) * cosw),
                      a * ((a + 1.0) - (a - 1.0) * cosw - s),
                      (a + 1.0) + (a - 1.0) * cosw + s,
                      -2.0 * ((a - 1.0) + (a + 1.0) * cosw),
                      (a + 1.0) + (a - 1.0) * cosw - s);
}

biquad::Coefficients highShelf2(double f, double gainDb, double q, double sr) noexcept
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const auto [cosw, alpha] = angle(f, q, sr);
    const double s = 2.0 * std::sqrt(a) * alpha;
    return normalised(a * ((a + 1.0) + (a - 1.0) * cosw + s),
                      -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw),
                      a * ((a + 1.0) + (a - 1.0) * cosw - s),
                      (a + 1.0) - (a - 1.0) * cosw + s,
                      2.0 * ((a - 1.0) - (a + 1.0) * cosw),
                      (a + 1.0) - (a - 1.0) * cosw - s);
}

// First-order shelves: H(s) = (s + sqrt(G)) / (s + 1/sqrt(G)) for the low shelf and
// G (s + 1/sqrt(G)) / (s + sqrt(G)) for the high shelf. Both pass sqrt(G) at the corner.
biquad::Coefficients lowShelf1(double f, double gainDb, double sr) noexcept
{
    const double root = std::pow(10.0, gainDb / 40.0);
    const double k = warped(f, sr);
    return normalised(1.0 + k * root, k * root - 1.0, 0.0,
                      1.0 + k / root, k / root - 1.0, 0.0);
}

biquad::Coefficients highShelf1(double f, double gainDb, double sr) noexcept
{
    const double root = std::pow(10.0, gainDb / 40.0);
    const double g = root * root;
    const double k = warped(f, sr);
    return normalised(g * (1.0 + k / root), g * (k / root - 1.0), 0.0,
                      1.0 + k * root, k * root - 1.0, 0.0);
}

biquad::Coefficients notch(double f, double q, double sr) noexcept
{
    const auto [cosw, alpha] = angle(f, q, sr);
    return normalised(1.0, -2.0 * cosw, 1.0, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

biquad::Coefficients bandPass(double f, double q, double sr) noexcept
{
    const auto [cosw, alpha] = angle(f, q, sr);
    return normalised(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

void addCut(Design& d, bool low, const Settings& s, double sr) noexcept
{
    const int order = orderOf(s.slope);
    if (order == 1) {
        d.sections[0] = low ? lowCut1(s.frequencyHz, sr) : highCut1(s.frequencyHz, sr);
        d.sectionCount = 1;
        return;
    }
    d.sectionCount = order / 2;
    for (int k = 0; k < d.sectionCount; ++k) {
        const double q = sectionQ(order, k, s.q);
        d.sections[static_cast<std::size_t>(k)] = low ? lowCut2(s.frequencyHz, q, sr)
                                                      : highCut2(s.frequencyHz, q, sr);
    }
}

void addShelf(Design& d, bool low, double gainDb, const Settings& s, double sr) noexcept
{
    const int order = orderOf(s.slope);
    if (order == 1) {
        d.sections[0] = low ? lowShelf1(s.frequencyHz, gainDb, sr) : highShelf1(s.frequencyHz, gainDb, sr);
        d.sectionCount = 1;
        return;
    }
    d.sectionCount = order / 2;
    const double share = gainDb / d.sectionCount;
    for (int k = 0; k < d.sectionCount; ++k) {
        const double q = sectionQ(order, k, s.q);
        d.sections[static_cast<std::size_t>(k)] = low ? lowShelf2(s.frequencyHz, share, q, sr)
                                                      : highShelf2(s.frequencyHz, share, q, sr);
    }
}

double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

} // namespace

bool usesGain(Shape shape) noexcept
{
    return shape == Shape::Bell || shape == Shape::LowShelf || shape == Shape::HighShelf
        || shape == Shape::TiltShelf;
}

bool usesSlope(Shape shape) noexcept
{
    return shape == Shape::LowShelf || shape == Shape::HighShelf || shape == Shape::LowCut
        || shape == Shape::HighCut || shape == Shape::TiltShelf;
}

int dbPerOctave(Slope slope) noexcept
{
    return 6 * orderOf(slope);
}

Settings sanitize(const Settings& settings, double sampleRate) noexcept
{
    const Settings defaults;
    Settings s = settings;
    const double rate = std::isfinite(sampleRate) && sampleRate > 1000.0 ? sampleRate : 48000.0;
    const double top = std::min(MaxFrequencyHz, rate * 0.49);
    s.frequencyHz = std::clamp(finiteOr(s.frequencyHz, defaults.frequencyHz), MinFrequencyHz, top);
    s.gainDb = std::clamp(finiteOr(s.gainDb, defaults.gainDb), MinGainDb, MaxGainDb);
    s.q = std::clamp(finiteOr(s.q, defaults.q), MinQ, MaxQ);
    switch (s.shape) {
    case Shape::Bell: case Shape::LowShelf: case Shape::HighShelf: case Shape::LowCut:
    case Shape::HighCut: case Shape::Notch: case Shape::BandPass: case Shape::TiltShelf:
        break;
    default:
        s.shape = defaults.shape;
    }
    switch (s.slope) {
    case Slope::Db6: case Slope::Db12: case Slope::Db24: case Slope::Db48:
        break;
    default:
        s.slope = defaults.slope;
    }
    return s;
}

Design design(const Settings& settings, double sampleRate) noexcept
{
    const double sr = std::isfinite(sampleRate) && sampleRate > 1000.0 ? sampleRate : 48000.0;
    const Settings s = sanitize(settings, sr);
    Design d;
    switch (s.shape) {
    case Shape::Bell:
        d.sections[0] = bell(s.frequencyHz, s.gainDb, s.q, sr);
        d.sectionCount = 1;
        break;
    case Shape::LowShelf:  addShelf(d, true, s.gainDb, s, sr); break;
    case Shape::HighShelf: addShelf(d, false, s.gainDb, s, sr); break;
    case Shape::LowCut:    addCut(d, true, s, sr); break;
    case Shape::HighCut:   addCut(d, false, s, sr); break;
    case Shape::Notch:
        d.sections[0] = notch(s.frequencyHz, s.q, sr);
        d.sectionCount = 1;
        break;
    case Shape::BandPass:
        d.sections[0] = bandPass(s.frequencyHz, s.q, sr);
        d.sectionCount = 1;
        break;
    case Shape::TiltShelf: {
        // A high shelf by the full gain, lowered by half of it: the low end moves by
        // -gain/2, the high end by +gain/2, and the corner stays at 0 dB.
        addShelf(d, false, s.gainDb, s, sr);
        const double trim = std::pow(10.0, -s.gainDb / 40.0);
        d.sections[0].b0 *= trim;
        d.sections[0].b1 *= trim;
        d.sections[0].b2 *= trim;
        break;
    }
    }
    return d;
}

double magnitude(const Design& design, double frequencyHz, double sampleRate) noexcept
{
    double total = 1.0;
    for (int k = 0; k < design.sectionCount; ++k)
        total *= biquad::magnitude(design.sections[static_cast<std::size_t>(k)], frequencyHz, sampleRate);
    return total;
}

double magnitudeDb(const Design& design, double frequencyHz, double sampleRate) noexcept
{
    return 20.0 * std::log10(std::max(magnitude(design, frequencyHz, sampleRate), 1e-15));
}

} // namespace eqband
