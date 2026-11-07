#include "testkit.h"
#include "plugins/chaoseq/chaoseqcurve.h"

#include <cmath>

TEST(chaoseqcurve, points_are_log_spaced_from_20_hz_to_20_khz)
{
    CHECK_NEAR(chaoseq::curveFrequency(0, 4), 20.0, 1e-9);
    CHECK_NEAR(chaoseq::curveFrequency(3, 4), 20000.0, 1e-6);
    CHECK_NEAR(chaoseq::curveFrequency(1, 4), 200.0, 1e-9);   // one decade per step
    CHECK_NEAR(chaoseq::curveFrequency(2, 4), 2000.0, 1e-9);
}

TEST(chaoseqcurve, empty_eq_is_a_flat_line)
{
    const auto c = chaoseq::curve({}, 48000.0, 64);
    CHECK(c.frequencies.size() == 64 && c.totalDb.size() == 64);
    for (double v : c.totalDb) CHECK(v == 0.0);
    for (const auto& band : c.bandDb) CHECK(band.empty());
}

TEST(chaoseqcurve, total_is_the_sum_of_active_bands_and_bypassed_bands_still_draw)
{
    chaoseq::Settings s;
    s.bands[2] = {true, false, false, chaoseq::Placement::Stereo, {eqband::Shape::Bell, 1000.0, 6.0, 1.0, eqband::Slope::Db12}};
    s.bands[5] = {true, false, false, chaoseq::Placement::Stereo, {eqband::Shape::LowShelf, 100.0, -4.0, eqband::DefaultQ, eqband::Slope::Db12}};
    s.bands[9] = {true, true, false, chaoseq::Placement::Stereo, {eqband::Shape::Bell, 5000.0, 12.0, 1.0, eqband::Slope::Db12}};
    s.outputGainDb = 10.0;   // not part of the curve
    const auto c = chaoseq::curve(s, 48000.0, 200);
    CHECK(c.bandDb[2].size() == 200 && c.bandDb[5].size() == 200 && c.bandDb[9].size() == 200);
    CHECK(c.bandDb[0].empty());
    for (std::size_t i = 0; i < c.totalDb.size(); ++i)
        CHECK_NEAR(c.totalDb[i], c.bandDb[2][i] + c.bandDb[5][i], 1e-9);
    // The bypassed band's own curve still peaks at its gain near 5 kHz.
    double peak = -100.0;
    for (double v : c.bandDb[9]) peak = std::max(peak, v);
    CHECK(peak > 11.5 && peak <= 12.0 + 1e-9);
}

TEST(chaoseqcurve, frequencies_past_nyquist_hold_the_last_real_value)
{
    chaoseq::Settings s;
    s.bands[0] = {true, false, false, chaoseq::Placement::Stereo, {eqband::Shape::HighShelf, 8000.0, 6.0, eqband::DefaultQ, eqband::Slope::Db12}};
    const auto c = chaoseq::curve(s, 22050.0, 100);   // Nyquist 11 kHz, curve goes to 20 kHz
    for (double v : c.totalDb) CHECK(std::isfinite(v));
    CHECK(c.totalDb.back() == c.totalDb[c.totalDb.size() - 2]);
}
