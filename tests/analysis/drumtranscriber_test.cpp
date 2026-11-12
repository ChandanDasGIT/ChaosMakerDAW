#include "testkit.h"
#include "analysis/drumtranscriber.h"
#include "drumrender.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

using namespace analysis;
using namespace drumrender;

namespace {

int drumOf(int key) { return key == OpenHatKey ? ClosedHatKey : key; }

struct Score {
    int expected = 0, found = 0, matched = 0;
    double recall() const { return expected ? double(matched) / expected : 0.0; }
    double precision() const { return found ? double(matched) / found : 0.0; }
};

// Each drum's found notes against the played hits: a match is the same drum within 30 ms.
Score score(const DrumPattern& p, const std::vector<Timed>& played, int key)
{
    Score s;
    std::vector<double> found;
    for (const auto& n : p.notes)
        if (drumOf(n.note) == key) found.push_back(p.firstBarSeconds + n.startBeat * p.secondsPerQuarter);
    std::vector<bool> used(found.size(), false);
    s.found = int(found.size());
    for (const auto& h : played) {
        if (drumOf(h.key) != key) continue;
        ++s.expected;
        for (std::size_t i = 0; i < found.size(); ++i)
            if (!used[i] && std::abs(found[i] - h.seconds) < 0.03) { used[i] = true; ++s.matched; break; }
    }
    return s;
}

void report(const char* what, const DrumAnalysis& a, const std::vector<Timed>& played)
{
    const auto k = score(a.pattern, played, Kick), s = score(a.pattern, played, Snare), h = score(a.pattern, played, Hat);
    std::printf("    %s: %.2f BPM %d/%d, bar at %.3f s; kick %.0f%%/%.0f%%, snare %.0f%%/%.0f%%, hat %.0f%%/%.0f%% (recall/precision)\n",
                what, a.pattern.bpm, a.pattern.numerator, a.pattern.denominator, a.pattern.firstBarSeconds,
                100 * k.recall(), 100 * k.precision(), 100 * s.recall(), 100 * s.precision(), 100 * h.recall(), 100 * h.precision());
}

struct Case {
    Groove groove;
    double bpm;
    double seconds = 20.0;
    double start = 0.4;
};

std::vector<Timed> played(const Case& c)
{
    const int bars = int((c.seconds - c.start - 0.5) / (c.groove.barQuarters() * 60.0 / c.bpm));
    return timed(c.groove, c.bpm, bars, c.start);
}

// Bar 1 must start on a played bar line.
bool barOnTheGrid(const DrumPattern& p, const Case& c)
{
    const double bar = c.groove.barQuarters() * 60.0 / c.bpm;
    const double x = (p.firstBarSeconds - c.start) / bar;
    return std::abs(x - std::round(x)) * bar < 0.03;
}

} // namespace

TEST(drumtranscriber, a_rock_loop_gives_its_tempo_meter_bar_and_hits)
{
    const Case c{rock(), 110.0};
    const auto hits = played(c);
    const auto a = analyseDrums(electroDrums(hits, c.seconds), Rate);
    CHECK(a.has_value());
    if (!a) return;
    report("rock 110", *a, hits);
    CHECK(std::abs(a->pattern.bpm - 110.0) < 0.1);
    CHECK(a->pattern.numerator == 4 && a->pattern.denominator == 4);
    CHECK(barOnTheGrid(a->pattern, c));
    for (int key : {Kick, Snare, Hat}) {
        const auto s = score(a->pattern, hits, key);
        CHECK(s.recall() >= 0.9);
        CHECK(s.precision() >= 0.9);
    }
    CHECK(std::abs(a->pattern.gridQuarters - 0.5) < 1e-9 || std::abs(a->pattern.gridQuarters - 0.25) < 1e-9);
    CHECK(a->pattern.kicks > 0 && a->pattern.snares > 0 && a->pattern.hats > 0);
    // Notes are on the drum keys, inside their bars, in time order.
    for (std::size_t i = 0; i < a->pattern.notes.size(); ++i) {
        const auto& n = a->pattern.notes[i];
        CHECK(n.note == KickKey || n.note == SnareKey || n.note == ClosedHatKey || n.note == OpenHatKey);
        CHECK(n.velocity >= 1 && n.velocity <= 127 && n.startBeat >= 0.0);
        CHECK(n.startBeat < a->pattern.bars * a->pattern.barQuarters());
        if (i) CHECK(n.startBeat >= a->pattern.notes[i - 1].startBeat);
    }
}

TEST(drumtranscriber, three_four_and_six_eight_are_told_from_four_four)
{
    for (const auto& [c, num, den] : {std::tuple{Case{waltz(), 132.0}, 3, 4}, std::tuple{Case{sixEight(), 68.0, 24.0}, 6, 8},
                                       std::tuple{Case{rock(), 88.0}, 4, 4}}) {
        const auto hits = played(c);
        const auto a = analyseDrums(electroDrums(hits, c.seconds), Rate);
        CHECK(a.has_value());
        if (!a) continue;
        const std::string what = std::to_string(num) + "/" + std::to_string(den);
        report(what.c_str(), *a, hits);
        CHECK(a->pattern.numerator == num && a->pattern.denominator == den);
        CHECK(std::abs(a->pattern.bpm - c.bpm) < 0.2);                   // quarter notes per minute
        CHECK(barOnTheGrid(a->pattern, c));
        for (int key : {Kick, Snare, Hat}) CHECK(score(a->pattern, hits, key).recall() >= 0.9 && score(a->pattern, hits, key).precision() >= 0.9);
    }
}

TEST(drumtranscriber, a_song_with_pad_and_bass_still_gives_the_beat_and_most_hits)
{
    for (double bpm : {96.0, 124.0}) {
        const Case c{rock(), bpm, 24.0};
        const auto hits = played(c);
        auto sound = electroDrums(hits, c.seconds);
        const int bars = int(hits.size() / rock().bar.size());
        mixInto(sound, band(bpm, bars, 4.0, c.start, c.seconds), 1.0f);
        const auto a = analyseDrums(sound, Rate);
        CHECK(a.has_value());
        if (!a) continue;
        report(bpm < 100 ? "song 96" : "song 124", *a, hits);
        CHECK(std::abs(a->pattern.bpm - bpm) < 0.1);
        CHECK(a->pattern.numerator == 4);
        CHECK(barOnTheGrid(a->pattern, c));
        for (int key : {Kick, Snare, Hat}) {
            CHECK(score(a->pattern, hits, key).recall() >= 0.9);
            CHECK(score(a->pattern, hits, key).precision() >= 0.9);
        }
    }
}

TEST(drumtranscriber, the_acoustic_kit_alone_and_in_a_song)
{
    if (acousticKitFolder().empty()) { std::printf("    skipped: the acoustic kit is not here\n"); return; }
    const Case c{rock(), 100.0, 22.0};
    const auto hits = played(c);
    auto sound = acousticDrums(hits, c.seconds);
    CHECK(!sound.empty());
    auto a = analyseDrums(sound, Rate);
    CHECK(a.has_value());
    if (!a) return;
    report("acoustic 100", *a, hits);
    CHECK(std::abs(a->pattern.bpm - 100.0) < 0.1 && a->pattern.numerator == 4);
    CHECK(barOnTheGrid(a->pattern, c));
    for (int key : {Kick, Snare, Hat}) {
        CHECK(score(a->pattern, hits, key).recall() >= 0.85);
        CHECK(score(a->pattern, hits, key).precision() >= 0.85);
    }
    mixInto(sound, band(100.0, int(hits.size() / rock().bar.size()), 4.0, c.start, c.seconds), 1.0f);
    a = analyseDrums(sound, Rate);
    CHECK(a.has_value());
    if (!a) return;
    report("acoustic song 100", *a, hits);
    // The hardest case here: a quiet real kit under a pad, whose highs hide part of the hi-hats.
    CHECK(std::abs(a->pattern.bpm - 100.0) < 0.1 && a->pattern.numerator == 4);
    CHECK(barOnTheGrid(a->pattern, c));
    CHECK(score(a->pattern, hits, Kick).recall() >= 0.9 && score(a->pattern, hits, Kick).precision() >= 0.9);
    CHECK(score(a->pattern, hits, Snare).recall() >= 0.9 && score(a->pattern, hits, Snare).precision() >= 0.7);
    CHECK(score(a->pattern, hits, Hat).recall() >= 0.6 && score(a->pattern, hits, Hat).precision() >= 0.9);
}

TEST(drumtranscriber, retempo_doubles_and_halves_without_new_hits)
{
    const Case c{rock(), 90.0};
    const auto hits = played(c);
    const auto a = analyseDrums(electroDrums(hits, c.seconds), Rate);
    CHECK(a.has_value());
    if (!a) return;
    const auto twice = retempo(*a, 2.0);
    CHECK(twice.has_value());
    if (twice) {
        CHECK(std::abs(twice->pattern.bpm - 180.0) < 0.3);
        CHECK(twice->hits.size() == a->hits.size());
        // The same hits at the same times, counted twice as fast.
        CHECK(score(twice->pattern, hits, Snare).recall() >= 0.9);
        const auto back = retempo(*twice, 0.5);
        CHECK(back && std::abs(back->pattern.bpm - 90.0) < 0.2);
    }
    CHECK(!retempo(*a, 0.0));
    CHECK(!retempo(*a, 10.0));
}

TEST(drumtranscriber, short_or_cancelled_sounds_give_nothing)
{
    CHECK(!analyseDrums(std::vector<float>(std::size_t(3.0 * Rate), 0.0f), Rate));
    std::atomic<bool> cancel{true};
    Progress p;
    p.cancel = &cancel;
    CHECK(!analyseDrums(std::vector<float>(std::size_t(6.0 * Rate), 0.1f), Rate, p));
    // Progress goes up to 1.
    double last = 0.0;
    bool rising = true;
    Progress q;
    q.report = [&](double v) { rising = rising && v >= last - 1e-12; last = v; };
    const auto a = analyseDrums(electroDrums(timed(rock(), 120.0, 3, 0.2), 6.0), Rate, q);
    CHECK(a.has_value() && rising && std::abs(last - 1.0) < 1e-9);
}

TEST(drumtranscriber, typical_bars_pick_the_bars_that_repeat_most)
{
    DrumPattern p;
    p.gridQuarters = 0.5;
    p.bars = 6;
    const auto add = [&](double beat, int key) { MidiNote n; n.note = key; n.startBeat = beat; n.lengthBeats = 0.25; p.notes.push_back(n); };
    // Bar 0 is a fill; bars 1..5 the beat (kick 1, snare 3).
    for (double b : {0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5}) add(b, SnareKey);
    for (int bar = 1; bar < 6; ++bar) { add(bar * 4.0, KickKey); add(bar * 4.0 + 2.0, SnareKey); }
    int start = -1;
    const auto two = typicalBars(p, 2, &start);
    CHECK(start >= 1);
    CHECK(two.size() == 4);
    CHECK(two[0].startBeat == 0.0 && two[0].note == KickKey && two[0].id == 1);
    p.bars = 2;
    CHECK(typicalBars(p, 2).size() == p.notes.size());
    CHECK(typicalBars(DrumPattern{}, 2).empty());
}
