#include "testkit.h"
#include "dsp/levelscale.h"

#include <limits>

TEST(levelscale, finite_gain_and_decibel_conversions_preserve_unity_and_round_trip)
{
    CHECK_NEAR(levelscale::gainToDb(1), 0, 1e-12);
    CHECK_NEAR(levelscale::gainToDb(0.5), -6.020599913279624, 1e-12);
    CHECK_NEAR(levelscale::dbToGain(0), 1, 1e-12);
    for (double db : {-120.0, -60.0, -12.0, 0.0, 6.0})
        CHECK_NEAR(levelscale::gainToDb(levelscale::dbToGain(db)), db, 1e-12);
    CHECK(levelscale::gainToDb(0) == -120);
    CHECK(levelscale::gainToDb(1e-12) == -120);
}

TEST(levelscale, invalid_values_never_propagate_nan_or_infinity_to_meter_state)
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    for (double gain : {-1.0, nan, inf}) CHECK(levelscale::gainToDb(gain) == -120);
    for (double db : {nan, inf, -inf}) {
        CHECK(levelscale::dbToGain(db) == 0);
        CHECK(levelscale::normalizedLevel(db) == 0);
        CHECK(levelscale::classify(db) == levelscale::Band::Normal);
    }
    CHECK(levelscale::dbToGain(std::numeric_limits<double>::max()) == std::numeric_limits<double>::max());
    CHECK(levelscale::dbToGain(-std::numeric_limits<double>::max()) == 0);
}

TEST(levelscale, meter_normalization_and_warning_boundaries_match_existing_controls)
{
    CHECK(levelscale::normalizedLevel(-120) == 0);
    CHECK(levelscale::normalizedLevel(-60) == 0);
    CHECK_NEAR(levelscale::normalizedLevel(-30), 0.5, 1e-12);
    CHECK_NEAR(levelscale::normalizedLevel(-12), 0.8, 1e-12);
    CHECK(levelscale::normalizedLevel(6) == 1);
    CHECK(levelscale::classify(-12) == levelscale::Band::Normal);
    CHECK(levelscale::classify(-11.99) == levelscale::Band::Warning);
    CHECK(levelscale::classify(-0.01) == levelscale::Band::Warning);
    CHECK(levelscale::classify(0) == levelscale::Band::Clip);
}

TEST(levelscale, peak_decay_uses_elapsed_time_and_retains_new_louder_peaks)
{
    CHECK_NEAR(levelscale::decayedPeakDb(-6, 0, 0.05), -7.2, 1e-12);
    CHECK_NEAR(levelscale::decayedPeakDb(-30, 1, 0.05), 0, 1e-12);
    double repeated = -6;
    for (int i = 0; i < 10; ++i) repeated = levelscale::decayedPeakDb(repeated, 0, 0.05);
    CHECK_NEAR(repeated, levelscale::decayedPeakDb(-6, 0, 0.5), 1e-10);
    CHECK(levelscale::decayedPeakDb(-119, 0, 1) == -120);
    CHECK(levelscale::decayedPeakDb(-6, 0, -1) == -6);
    CHECK(levelscale::decayedPeakDb(-6, 0, std::numeric_limits<double>::quiet_NaN()) == -6);
    CHECK(levelscale::decayedPeakDb(std::numeric_limits<double>::quiet_NaN(), 0, 1) == -120);
}
