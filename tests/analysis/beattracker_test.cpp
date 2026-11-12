#include "testkit.h"
#include "analysis/beattracker.h"
#include "analysis/hpss.h"
#include "analysis/onsets.h"
#include "analysis/spectrogram.h"
#include "drumrender.h"

#include <cmath>
#include <vector>

using namespace analysis;
using namespace drumrender;

namespace {

Envelopes envelopesOf(const std::vector<float>& sound)
{
    const auto s = bandSpectrogram(sound, Rate);
    return envelopes(s, separate(s).percussive);
}

// The beat on a grid line: the found beats land within 25 ms of the groove's quarter notes.
bool onTheGrid(const Beat& beat, double bpm, double startSeconds)
{
    const double quarter = 60.0 / bpm;
    const double t = beat.secondsOf(0) - startSeconds;
    const double off = std::abs(t / quarter - std::round(t / quarter)) * quarter;
    return off < 0.025;
}

} // namespace

TEST(beattracker, rock_beats_at_several_tempos_are_found_to_a_tenth_of_a_bpm)
{
    for (double bpm : {72.0, 96.0, 120.0, 133.0, 158.0}) {
        const double seconds = 20.0, start = 0.37;
        const auto hits = timed(rock(), bpm, int((seconds - start) / (4 * 60.0 / bpm)), start);
        const auto e = envelopesOf(electroDrums(hits, seconds));
        const auto beat = trackBeats(e.total, e.frameRate);
        std::printf("    %.1f BPM: found %.3f, beat at %.3f s\n", bpm, beat.bpm(), beat.secondsOf(0));
        CHECK(std::abs(beat.bpm() - bpm) < 0.1);
        CHECK(onTheGrid(beat, bpm, start));
    }
}

TEST(beattracker, an_uneven_tempo_is_kept_when_not_near_a_whole_number)
{
    const double bpm = 104.5, seconds = 24.0;
    const auto hits = timed(rock(), bpm, int(seconds / (4 * 60.0 / bpm)) - 1, 0.5);
    const auto e = envelopesOf(electroDrums(hits, seconds));
    const auto beat = trackBeats(e.total, e.frameRate);
    std::printf("    104.5 BPM: found %.3f\n", beat.bpm());
    CHECK(std::abs(beat.bpm() - bpm) < 0.1);
}

TEST(beattracker, near_bpm_doubles_or_halves_and_short_or_silent_sounds_give_nothing)
{
    const double bpm = 100.0, seconds = 16.0;
    const auto e = envelopesOf(electroDrums(timed(rock(), bpm, 6, 0.2), seconds));
    const auto beat = trackBeats(e.total, e.frameRate);
    CHECK(std::abs(beat.bpm() - 100.0) < 0.1);
    CHECK(std::abs(trackBeats(e.total, e.frameRate, 200.0).bpm() - 200.0) < 0.2);
    CHECK(std::abs(trackBeats(e.total, e.frameRate, 50.0).bpm() - 50.0) < 0.1);
    CHECK(trackBeats(std::vector<float>(100, 0.0f), 100.0).strength == 0.0);
    CHECK(trackBeats(std::vector<float>(3000, 0.0f), 100.0).strength == 0.0);
}

TEST(beattracker, a_three_minute_song_stays_locked_to_the_end)
{
    // A long recording needs the period to a hundredth of a percent: the fit grows its window.
    const double bpm = 117.0, seconds = 180.0, start = 0.4;
    const auto hits = timed(rock(), bpm, int((seconds - 1.0) / (4 * 60.0 / bpm)), start);
    const auto e = envelopesOf(electroDrums(hits, seconds));
    const auto beat = trackBeats(e.total, e.frameRate);
    std::printf("    3 min at 117: found %.4f, strength %.2f\n", beat.bpm(), beat.strength);
    CHECK(std::abs(beat.bpm() - bpm) < 0.01);
    CHECK(onTheGrid(beat, bpm, start));
    // The last beats too: the beat near the end lies on a quarter note.
    const int last = int((seconds - 2.0 - beat.secondsOf(0)) / (60.0 / bpm));
    const double quarter = 60.0 / bpm, t = beat.secondsOf(last) - start;
    CHECK(std::abs(t / quarter - std::round(t / quarter)) * quarter < 0.025);
}
