#include "testkit.h"
#include "dsp/eqbanddesign.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr double kHalfPowerDb = -3.0102999566;   // 20 log10(1 / sqrt 2)
constexpr eqband::Slope kSlopes[] = {eqband::Slope::Db6, eqband::Slope::Db12,
                                     eqband::Slope::Db24, eqband::Slope::Db48};

eqband::Design make(eqband::Shape shape, double hz, double gainDb = 0.0,
                    double q = eqband::DefaultQ, eqband::Slope slope = eqband::Slope::Db12)
{
    return eqband::design({shape, hz, gainDb, q, slope}, kRate);
}

double db(const eqband::Design& d, double hz)
{
    return eqband::magnitudeDb(d, hz, kRate);
}

// Response on a log sweep from 20 Hz to 20 kHz.
std::vector<std::pair<double, double>> sweep(const eqband::Design& d)
{
    std::vector<std::pair<double, double>> points;
    for (int i = 0; i <= 600; ++i) {
        const double hz = 20.0 * std::pow(1000.0, i / 600.0);
        points.emplace_back(hz, db(d, hz));
    }
    return points;
}

// Width in octaves of the part of the curve where the gain moves from 10% to 90% of its range.
double transitionOctaves(const eqband::Design& d, double fullDb)
{
    double first = 0.0, last = 0.0;
    for (const auto& [hz, gain] : sweep(d)) {
        const double share = gain / fullDb;
        if (share > 0.1 && share < 0.9) {
            if (first == 0.0) first = hz;
            last = hz;
        }
    }
    return std::log2(last / first);
}

// Runs a steady sine through the sections and returns the output level change in dB.
double measuredDb(const eqband::Design& d, double hz)
{
    std::array<biquad::State, eqband::MaxSections> state{};
    constexpr double twoPi = 6.283185307179586;
    const int settle = 48000, measure = 48000;
    double inSum = 0.0, outSum = 0.0;
    for (int n = 0; n < settle + measure; ++n) {
        const double in = std::sin(twoPi * hz * n / kRate);
        double out = in;
        for (int k = 0; k < d.sectionCount; ++k)
            out = biquad::process(d.sections[static_cast<std::size_t>(k)], state[static_cast<std::size_t>(k)], out);
        if (n >= settle) {
            inSum += in * in;
            outSum += out * out;
        }
    }
    return 10.0 * std::log10(outSum / inSum);
}

} // namespace

TEST(eqbanddesign, bell_hits_its_gain_at_the_centre_and_is_flat_far_away)
{
    for (double gain : {6.0, -12.0, 24.0}) {
        const auto bell = make(eqband::Shape::Bell, 1000.0, gain, 1.0);
        CHECK(bell.sectionCount == 1);
        CHECK_NEAR(db(bell, 1000.0), gain, 1e-6);
        CHECK_NEAR(db(bell, 20.0), 0.0, 0.1);
        CHECK_NEAR(db(bell, 20000.0), 0.0, 0.2);
    }
    // A higher Q is narrower: less boost one octave away.
    const auto wide = make(eqband::Shape::Bell, 1000.0, 12.0, 0.5);
    const auto narrow = make(eqband::Shape::Bell, 1000.0, 12.0, 4.0);
    CHECK(db(narrow, 2000.0) < db(wide, 2000.0) - 3.0);
    // Zero gain is a straight line.
    const auto flat = make(eqband::Shape::Bell, 300.0, 0.0, 2.0);
    for (const auto& [hz, gain] : sweep(flat))
        CHECK_NEAR(gain, 0.0, 1e-9);
}

TEST(eqbanddesign, cuts_are_half_power_at_the_corner_for_every_slope)
{
    for (auto slope : kSlopes) {
        const auto low = make(eqband::Shape::LowCut, 1000.0, 0.0, eqband::DefaultQ, slope);
        const auto high = make(eqband::Shape::HighCut, 1000.0, 0.0, eqband::DefaultQ, slope);
        CHECK(low.sectionCount == std::max(1, eqband::dbPerOctave(slope) / 12));
        CHECK_NEAR(db(low, 1000.0), kHalfPowerDb, 1e-6);
        CHECK_NEAR(db(high, 1000.0), kHalfPowerDb, 1e-6);
        CHECK_NEAR(db(low, 15000.0), 0.0, 0.05);   // pass band stays flat
        CHECK_NEAR(db(high, 50.0), 0.0, 0.05);
        // Default Q never bumps above 0 dB.
        for (const auto& [hz, gain] : sweep(low)) CHECK(gain < 1e-9);
        for (const auto& [hz, gain] : sweep(high)) CHECK(gain < 1e-9);
    }
}

TEST(eqbanddesign, cuts_fall_at_their_slope)
{
    for (auto slope : kSlopes) {
        const double perOctave = 20.0 * std::log10(2.0) * eqband::dbPerOctave(slope) / 6.0;
        // Low cut at 2 kHz: one octave deep in the stop band, from 125 Hz down to 62.5 Hz.
        const auto low = make(eqband::Shape::LowCut, 2000.0, 0.0, eqband::DefaultQ, slope);
        CHECK_NEAR(db(low, 125.0) - db(low, 62.5), perOctave, 0.5);
        // High cut at 100 Hz: from 1.6 kHz up to 3.2 kHz. A digital filter squeezes the
        // frequency axis towards Nyquist (tan warping), so that octave acts slightly wider.
        const auto high = make(eqband::Shape::HighCut, 100.0, 0.0, eqband::DefaultQ, slope);
        const double pi = 3.14159265358979323846;
        const double warpedOctaves = std::log2(std::tan(pi * 3200.0 / kRate) / std::tan(pi * 1600.0 / kRate));
        CHECK_NEAR(db(high, 1600.0) - db(high, 3200.0), perOctave * warpedOctaves, 0.1);
    }
}

TEST(eqbanddesign, cut_q_adds_a_resonant_bump_at_the_corner)
{
    // At 12 dB/oct the gain at the corner equals Q.
    const auto low = make(eqband::Shape::LowCut, 500.0, 0.0, 4.0, eqband::Slope::Db12);
    CHECK_NEAR(db(low, 500.0), 20.0 * std::log10(4.0), 1e-6);
    const auto high = make(eqband::Shape::HighCut, 500.0, 0.0, 4.0, eqband::Slope::Db12);
    CHECK_NEAR(db(high, 500.0), 20.0 * std::log10(4.0), 1e-6);
    // Steeper slopes resonate too.
    for (auto slope : {eqband::Slope::Db24, eqband::Slope::Db48}) {
        const auto steep = make(eqband::Shape::LowCut, 500.0, 0.0, 4.0, slope);
        double peak = -100.0;
        for (const auto& [hz, gain] : sweep(steep)) peak = std::max(peak, gain);
        CHECK(peak > 6.0);
    }
    // The 6 dB/oct cut has no resonance: Q does nothing.
    const auto gentle = make(eqband::Shape::LowCut, 500.0, 0.0, 4.0, eqband::Slope::Db6);
    CHECK_NEAR(db(gentle, 500.0), kHalfPowerDb, 1e-6);
}

TEST(eqbanddesign, shelves_reach_full_gain_with_half_at_the_corner)
{
    for (auto slope : kSlopes) {
        for (double gain : {12.0, -9.0}) {
            const auto low = make(eqband::Shape::LowShelf, 500.0, gain, eqband::DefaultQ, slope);
            CHECK_NEAR(db(low, 500.0), gain / 2.0, 1e-6);
            CHECK_NEAR(db(low, 10.0), gain, 0.3);
            CHECK_NEAR(db(low, 20000.0), 0.0, 0.3);

            const auto high = make(eqband::Shape::HighShelf, 2000.0, gain, eqband::DefaultQ, slope);
            CHECK_NEAR(db(high, 2000.0), gain / 2.0, 1e-6);
            CHECK_NEAR(db(high, 20.0), 0.0, 0.3);
            CHECK_NEAR(db(high, 23000.0), gain, 0.3);

            // Default Q: no overshoot past either end.
            for (const auto& curve : {sweep(low), sweep(high)})
                for (const auto& [hz, value] : curve)
                    CHECK(value >= std::min(0.0, gain) - 1e-9 && value <= std::max(0.0, gain) + 1e-9);
        }
    }
}

TEST(eqbanddesign, steeper_shelves_change_over_fewer_octaves)
{
    double previous = 100.0;
    for (auto slope : kSlopes) {
        const auto shelf = make(eqband::Shape::LowShelf, 1000.0, 12.0, eqband::DefaultQ, slope);
        const double width = transitionOctaves(shelf, 12.0);
        CHECK(width < previous * 0.8);
        previous = width;
    }
}

TEST(eqbanddesign, tilt_shelf_pivots_around_its_frequency)
{
    for (auto slope : kSlopes) {
        const auto tilt = make(eqband::Shape::TiltShelf, 1000.0, 12.0, eqband::DefaultQ, slope);
        CHECK_NEAR(db(tilt, 1000.0), 0.0, 1e-6);
        CHECK_NEAR(db(tilt, 10.0), -6.0, 0.3);
        CHECK_NEAR(db(tilt, 23000.0), 6.0, 0.3);
    }
    const auto down = make(eqband::Shape::TiltShelf, 1000.0, -8.0);
    CHECK_NEAR(db(down, 10.0), 4.0, 0.3);
    CHECK_NEAR(db(down, 23000.0), -4.0, 0.3);
}

TEST(eqbanddesign, notch_removes_its_frequency_and_band_pass_keeps_only_it)
{
    const auto notch = make(eqband::Shape::Notch, 1000.0, 0.0, 4.0);
    CHECK(eqband::magnitude(notch, 1000.0, kRate) < 1e-9);
    CHECK_NEAR(db(notch, 100.0), 0.0, 0.1);
    CHECK_NEAR(db(notch, 10000.0), 0.0, 0.1);

    const auto pass = make(eqband::Shape::BandPass, 1000.0, 0.0, 4.0);
    CHECK_NEAR(db(pass, 1000.0), 0.0, 1e-6);
    CHECK(db(pass, 100.0) < -20.0);
    CHECK(db(pass, 10000.0) < -20.0);
    for (const auto& [hz, gain] : sweep(pass)) CHECK(gain < 1e-9);
}

TEST(eqbanddesign, sanitize_brings_bad_values_into_range)
{
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    const auto s = eqband::sanitize({eqband::Shape::Bell, nan, nan, nan, eqband::Slope::Db24}, kRate);
    CHECK(s.frequencyHz == 1000.0 && s.gainDb == 0.0 && s.q == eqband::DefaultQ);

    const auto big = eqband::sanitize({eqband::Shape::Bell, 1e9, 99.0, 1000.0, eqband::Slope::Db6}, 44100.0);
    CHECK_NEAR(big.frequencyHz, 44100.0 * 0.49, 1e-9);   // below Nyquist at 44.1 kHz
    CHECK(big.gainDb == eqband::MaxGainDb && big.q == eqband::MaxQ);

    const auto small = eqband::sanitize({eqband::Shape::Bell, 1.0, -99.0, 0.0, eqband::Slope::Db6}, kRate);
    CHECK(small.frequencyHz == eqband::MinFrequencyHz);
    CHECK(small.gainDb == eqband::MinGainDb && small.q == eqband::MinQ);

    const auto badEnums = eqband::sanitize({static_cast<eqband::Shape>(99), 500.0, 0.0, 1.0,
                                            static_cast<eqband::Slope>(-1)}, kRate);
    CHECK(badEnums.shape == eqband::Shape::Bell && badEnums.slope == eqband::Slope::Db12);

    // Out-of-range input still designs a stable, finite filter.
    const auto edge = eqband::design({eqband::Shape::HighCut, 1e9, 0.0, 1e9, eqband::Slope::Db48}, kRate);
    for (const auto& [hz, gain] : sweep(edge)) CHECK(std::isfinite(gain));
    CHECK(eqband::usesGain(eqband::Shape::TiltShelf) && !eqband::usesGain(eqband::Shape::Notch));
    CHECK(eqband::usesSlope(eqband::Shape::LowCut) && !eqband::usesSlope(eqband::Shape::Bell));
}

TEST(eqbanddesign, filtering_a_sine_matches_the_computed_response)
{
    const auto bell = make(eqband::Shape::Bell, 300.0, 6.0, 2.0);
    CHECK_NEAR(measuredDb(bell, 300.0), db(bell, 300.0), 0.05);
    const auto steep = make(eqband::Shape::LowCut, 1000.0, 0.0, eqband::DefaultQ, eqband::Slope::Db48);
    CHECK_NEAR(measuredDb(steep, 600.0), db(steep, 600.0), 0.05);
    const auto shelf = make(eqband::Shape::HighShelf, 40.0, -10.0, eqband::DefaultQ, eqband::Slope::Db24);
    CHECK_NEAR(measuredDb(shelf, 25.0), db(shelf, 25.0), 0.05);
}
