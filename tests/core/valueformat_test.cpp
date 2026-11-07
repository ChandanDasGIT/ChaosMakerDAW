#include "testkit.h"
#include "core/valueformat.h"

#include <limits>

TEST(valueformat, numbers_and_units_use_canonical_locale_independent_text)
{
    CHECK(valueformat::number(120) == "120");
    CHECK(valueformat::number(97.25) == "97.25");
    CHECK(valueformat::number(0.01) == "0.01");
    CHECK(valueformat::number(10000) == "10000");
    CHECK(valueformat::number(-0.0) == "0");
    CHECK(valueformat::withUnit(-6, 1, "dB") == "-6.0 dB");
    CHECK(valueformat::withUnit(137.5, 2, "Hz") == "137.50 Hz");
    CHECK(valueformat::withUnit(2, 0, "") == "2");
}

TEST(valueformat, fixed_precision_preserves_existing_decimal_rounding_and_zero_display)
{
    CHECK(valueformat::fixed(1.25, 1) == "1.3");
    CHECK(valueformat::fixed(-1.25, 1) == "-1.3");
    CHECK(valueformat::fixed(2.55, 1) == "2.5");
    CHECK(valueformat::fixed(2.675, 2) == "2.67");
    CHECK(valueformat::fixed(1.005, 2) == "1.00");
    CHECK(valueformat::fixed(-0.0, 1) == "0.0");
    CHECK(valueformat::fixed(-0.001, 1) == "-0.0");
    CHECK(valueformat::fixed(0.000001, 6) == "0.000001");
}

TEST(valueformat, invalid_values_and_meter_silence_have_explicit_display_text)
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    CHECK(valueformat::number(nan) == "—");
    CHECK(valueformat::fixed(inf, 1) == "—");
    CHECK(valueformat::fixed(1, -1) == "—");
    CHECK(valueformat::fixed(1, 7) == "—");
    CHECK(valueformat::withUnit(nan, 1, "dB") == "—");
    CHECK(valueformat::meterLevel(-120) == "−∞");
    CHECK(valueformat::meterLevel(-90) == "−∞");
    CHECK(valueformat::meterLevel(-inf) == "−∞");
    CHECK(valueformat::meterLevel(-89.9) == "-89.9");
    CHECK(valueformat::meterLevel(nan) == "—");
}

TEST(valueformat, bar_beat_position_follows_the_meter)
{
    // 4/4: 4 quarter beats per bar
    CHECK(valueformat::barBeatPosition(0, 4, 1) == "1.1.00");
    CHECK(valueformat::barBeatPosition(1.5, 4, 1) == "1.2.50");
    CHECK(valueformat::barBeatPosition(4, 4, 1) == "2.1.00");
    // 3/4: bar 2 starts at beat 3
    CHECK(valueformat::barBeatPosition(3, 3, 1) == "2.1.00");
    // 6/8: 3 quarters per bar, counted in eighths (0.5)
    CHECK(valueformat::barBeatPosition(3.75, 3, 0.5) == "2.2.50");
    CHECK(valueformat::barBeatPosition(2.9999999999, 3, 0.5) == "2.1.00");
    // 7/8: bars are 3.5 quarters long
    CHECK(valueformat::barBeatPosition(3.5, 3.5, 0.5) == "2.1.00");
    // Invalid input
    CHECK(valueformat::barBeatPosition(-1, 4, 1) == "—");
    CHECK(valueformat::barBeatPosition(1, 0, 1) == "—");
    CHECK(valueformat::barBeatPosition(std::numeric_limits<double>::quiet_NaN(), 4, 1) == "—");
}

TEST(valueformat, playback_position_floors_milliseconds_and_carries_minutes_and_hours)
{
    CHECK(valueformat::position(0) == "00:00:00.00");
    CHECK(valueformat::position(59.9996) == "00:00:59.99");   // floored, never rounded up
    CHECK(valueformat::position(60) == "00:01:00.00");
    CHECK(valueformat::position(3661.125) == "01:01:01.12");
    CHECK(valueformat::position(0.29) == "00:00:00.29");      // 0.29 * 100 is 28.999...
    CHECK(valueformat::position(360000) == "100:00:00.00");
    CHECK(valueformat::position(-0.001) == "—");
    CHECK(valueformat::position(std::numeric_limits<double>::quiet_NaN()) == "—");
    CHECK(valueformat::position(1e99) == "—");
}
