#include "testkit.h"
#include "analysis/chordfinder.h"
#include "chordrender.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

using namespace analysis;
using namespace chordrender;

namespace {

// The found chords as text, "C(4) Am(4) ...", the number being beats.
std::string line(const ChordAnalysis& a)
{
    std::string text;
    for (const auto& c : a.chords) text += (c.root < 0 ? std::string("-") : c.name) + "(" + std::to_string(c.slotCount) + ") ";
    return text;
}

// The share of the time from `fromSeconds` on where the found chord is the played one.
double rightShare(const ChordAnalysis& a, const std::vector<Bar>& bars, const std::vector<std::string>& names, double bpm, int beatsPerBar = 4)
{
    const double bar = beatsPerBar * 60.0 / bpm;
    double right = 0.0, all = 0.0;
    for (double t = 0.35; t < 0.3 + double(bars.size()) * bar; t += 0.05) {
        const auto b = std::size_t((t - 0.3) / bar);
        if (b >= bars.size()) break;
        all += 1.0;
        for (const auto& c : a.chords)
            if (t >= c.fromSeconds && t < c.toSeconds && c.name == names[b]) right += 1.0;
    }
    return all > 0.0 ? right / all : 0.0;
}

} // namespace

TEST(chordfinder, a_major_song_gives_its_chords_on_the_bars_and_its_key)
{
    // C | Am | F | G, four times, at 100 BPM.
    const std::vector<Bar> loop{{0, major}, {9, minor}, {5, major}, {7, major}};
    const std::vector<std::string> names{"C", "Am", "F", "G"};
    const auto bars = repeated(loop, 4);
    const auto a = analyseChords(song(bars, 100.0), rate);
    CHECK(a.has_value());
    if (!a) return;
    std::printf("    %.1f BPM %d/%d, %s: %s\n", a->bpm, a->numerator, a->denominator, chords::keyName(a->key).c_str(), line(*a).c_str());
    CHECK(a->hasBeat && std::abs(a->bpm - 100.0) < 0.5);
    CHECK(a->key == (chords::Key{0, false}));
    std::vector<std::string> played;
    for (int i = 0; i < 4; ++i) played.insert(played.end(), names.begin(), names.end());
    const double share = rightShare(*a, bars, played, 100.0);
    std::printf("    right %.0f%% of the time\n", 100.0 * share);
    CHECK(share >= 0.9);
    const auto common = commonChords(*a);
    CHECK(common.size() == 4);
    // The spans cover the sound in order, each with its name.
    CHECK(!a->chords.empty() && a->chords.front().fromSeconds == 0.0 && std::abs(a->chords.back().toSeconds - a->seconds) < 1e-9);
    for (std::size_t i = 1; i < a->chords.size(); ++i) {
        CHECK(a->chords[i].fromSeconds == a->chords[i - 1].toSeconds);
        CHECK(a->chords[i].fromSlot == a->chords[i - 1].fromSlot + a->chords[i - 1].slotCount);
    }
    CHECK(a->slotSeconds.size() == a->slotNotes.size() + 1 && a->slotBass.size() == a->slotNotes.size());
    CHECK(a->barSeconds > 2.3 && a->barSeconds < 2.5);
}

TEST(chordfinder, a_minor_song_in_a_flat_key_tuned_flat)
{
    // Gm | Bb | Cm | Gm, with A = 432 Hz: G minor, written with flats.
    const std::vector<Bar> loop{{7, minor}, {10, major}, {0, minor}, {7, minor}};
    const auto bars = repeated(loop, 4);
    const auto a = analyseChords(song(bars, 92.0, 432.0), rate);
    CHECK(a.has_value());
    if (!a) return;
    std::printf("    tuning %.1f cents, %s: %s\n", a->tuningCents, chords::keyName(a->key).c_str(), line(*a).c_str());
    CHECK(std::abs(a->tuningCents - 1200.0 * std::log2(432.0 / 440.0)) < 8.0);
    CHECK(a->key == (chords::Key{7, true}));
    std::vector<std::string> played;
    for (int i = 0; i < 4; ++i) played.insert(played.end(), {"Gm", "Bb", "Cm", "Gm"});
    CHECK(rightShare(*a, bars, played, 92.0) >= 0.9);
}

TEST(chordfinder, sevenths_are_named_only_when_asked_for)
{
    // C | G7 | C | G7 ...
    const std::vector<Bar> loop{{0, major}, {7, seventh}};
    const auto bars = repeated(loop, 6);
    auto a = analyseChords(song(bars, 110.0), rate);
    CHECK(a.has_value());
    if (!a) return;
    std::printf("    with sevenths: %s\n", line(*a).c_str());
    std::vector<std::string> played;
    for (int i = 0; i < 6; ++i) played.insert(played.end(), {"C", "G7"});
    CHECK(rightShare(*a, bars, played, 110.0) >= 0.85);
    ChordOptions triads;
    triads.sevenths = false;
    decodeChords(*a, triads);
    std::printf("    triads only:   %s\n", line(*a).c_str());
    played.clear();
    for (int i = 0; i < 6; ++i) played.insert(played.end(), {"C", "G"});
    CHECK(rightShare(*a, bars, played, 110.0) >= 0.9);
    for (const auto& c : a->chords) CHECK(c.quality == ChordQuality::None || c.quality == ChordQuality::Major || c.quality == ChordQuality::Minor);
}

TEST(chordfinder, a_drone_under_the_chords_does_not_hide_them)
{
    // D | G | A | D with a D held under everything (a pedal note, low and in the middle).
    const std::vector<Bar> loop{{2, major}, {7, major}, {9, major}, {2, major}};
    const auto bars = repeated(loop, 4);
    auto sound = song(bars, 96.0);
    for (std::size_t i = 0; i < sound.size(); ++i) {
        const double t = double(i) / rate;
        sound[i] += float(0.05 * std::sin(2.0 * std::numbers::pi * hzOf(38.0, 440.0) * t) + 0.04 * std::sin(2.0 * std::numbers::pi * hzOf(62.0, 440.0) * t));
    }
    const auto a = analyseChords(sound, rate);
    CHECK(a.has_value());
    if (!a) return;
    std::printf("    %s: %s\n", chords::keyName(a->key).c_str(), line(*a).c_str());
    std::vector<std::string> played;
    for (int i = 0; i < 4; ++i) played.insert(played.end(), {"D", "G", "A", "D"});
    CHECK(rightShare(*a, bars, played, 96.0) >= 0.8);
    CHECK(a->key == (chords::Key{2, false}));
}

TEST(chordfinder, silence_short_and_cancelled_sounds_and_names)
{
    CHECK(!analyseChords(std::vector<float>(std::size_t(2.0 * rate), 0.1f), rate).has_value());
    // Silence: slots without a beat, all "no chord".
    const auto silent = analyseChords(std::vector<float>(std::size_t(6.0 * rate), 0.0f), rate);
    CHECK(silent.has_value());
    if (silent) {
        CHECK(!silent->hasBeat && silent->chords.size() == 1 && silent->chords[0].root == -1 && silent->chords[0].name.empty());
        CHECK(commonChords(*silent).empty());
    }
    std::atomic<bool> cancel{true};
    Progress progress;
    progress.cancel = &cancel;
    CHECK(!analyseChords(song({{0, major}, {5, major}}, 100.0), rate, {}, progress).has_value());
    CHECK(chordName(10, ChordQuality::Major, chords::Key{7, true}) == "Bb");
    CHECK(chordName(6, ChordQuality::Minor, chords::Key{2, false}) == "F#m");
    CHECK(chordName(7, ChordQuality::Dominant7, chords::Key{0, false}) == "G7");
    CHECK(chordName(0, ChordQuality::Major7, chords::Key{0, false}) == "Cmaj7");
    CHECK(chordName(9, ChordQuality::Minor7, chords::Key{0, false}) == "Am7");
    CHECK(chordName(-1, ChordQuality::None, chords::Key{}).empty());
}
