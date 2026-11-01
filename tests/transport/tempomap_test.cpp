#include "testkit.h"

#include "transport/tempomap.h"

TEST(tempomap, beats_per_bar_follows_the_time_signature)
{
    TempoMap map;
    map.set(120.0, 4, 4, 48000.0);
    CHECK_NEAR(map.beatsPerBar(), 4.0, 1e-12);
    map.set(120.0, 3, 4, 48000.0);
    CHECK_NEAR(map.beatsPerBar(), 3.0, 1e-12);
    map.set(120.0, 6, 8, 48000.0);
    CHECK_NEAR(map.beatsPerBar(), 3.0, 1e-12);
    map.set(120.0, 7, 8, 48000.0);
    CHECK_NEAR(map.beatsPerBar(), 3.5, 1e-12);
}

TEST(tempomap, one_beat_at_120_bpm_is_half_a_second)
{
    TempoMap map;
    map.set(120.0, 4, 4, 48000.0);
    CHECK(map.sampleAt(1.0) == 24000);
    CHECK_NEAR(map.beatAt(24000), 1.0, 1e-12);
}

TEST(tempomap, positions_do_not_drift_late_in_a_song)
{
    TempoMap map;
    map.set(137.3, 4, 4, 48000.0);
    const double beat = 400000.0;
    CHECK_NEAR(map.beatAt(map.sampleAt(beat)), beat, 1e-4);   // within a sample
}
