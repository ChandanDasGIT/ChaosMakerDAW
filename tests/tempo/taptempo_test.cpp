#include "testkit.h"
#include "tempo/taptempo.h"

#include <cmath>

TEST(taptempo, steady_taps_give_their_tempo)
{
    TapTempo tap;
    CHECK(!tap.tap(10.0));                                 // one tap: nothing yet
    CHECK(tap.tap(10.5) == 120.0);                         // half a second apart
    for (int i = 2; i < 6; ++i) CHECK(tap.tap(10.0 + 0.5 * i) == 120.0);
    // Slightly uneven taps average out; the result is rounded to 0.1 BPM.
    TapTempo uneven;
    double t = 0.0;
    const double gaps[]{0.0, 0.62, 0.58, 0.61, 0.59, 0.60};
    std::optional<double> bpm;
    for (double g : gaps) { t += g; bpm = uneven.tap(t); }
    CHECK(bpm && std::abs(*bpm - 100.0) < 0.05);
    CHECK(*bpm == std::round(*bpm * 10.0) / 10.0);
}

TEST(taptempo, a_pause_or_a_new_pace_starts_again_and_the_average_follows_the_latest_taps)
{
    TapTempo tap;
    tap.tap(0.0); tap.tap(0.5); tap.tap(1.0);
    CHECK(!tap.tap(4.0));                                   // a 3 s pause: a new count
    CHECK(tap.taps() == 1);
    CHECK(tap.tap(4.75) == 80.0);

    TapTempo change;                                        // from 120 to 90 BPM without a pause
    double t = 0.0;
    for (int i = 0; i < 6; ++i) change.tap(t += 0.5);
    std::optional<double> bpm;
    for (int i = 0; i < 3; ++i) bpm = change.tap(t += 0.6667);
    CHECK(bpm && std::abs(*bpm - 90.0) < 0.1);              // follows the new pace at once

    TapTempo window;                                        // only the last 8 intervals count
    t = 0.0;
    for (int i = 0; i < 12; ++i) window.tap(t += 0.5);
    CHECK(window.taps() == 9);
}

TEST(taptempo, odd_input_is_ignored_and_the_range_is_kept)
{
    TapTempo tap;
    CHECK(!tap.tap(std::nan("")));
    tap.tap(1.0);
    CHECK(!tap.tap(1.0));                                   // the same moment
    CHECK(!tap.tap(0.5));                                   // backwards
    CHECK(tap.tap(1.1) == TapTempo::MaxBpm);                // 600 BPM is kept at the top
    TapTempo slow;
    slow.tap(0.0);
    CHECK(slow.tap(1.9) && *slow.tap(3.8) >= TapTempo::MinBpm);
    tap.reset();
    CHECK(tap.taps() == 0);
}
