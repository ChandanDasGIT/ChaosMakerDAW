#include "testkit.h"
#include "analysis/chromagram.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

using namespace analysis;

namespace {

constexpr double rate = 48000.0;

double hzOf(int midiNote, double a4 = 440.0) { return a4 * std::pow(2.0, (midiNote - 69) / 12.0); }

// A note with five overtones falling off, like a plucked or bowed string.
void addNote(std::vector<float>& out, double hz, double fromSeconds, double toSeconds, float level = 0.2f)
{
    const auto from = std::size_t(fromSeconds * rate), to = std::min(out.size(), std::size_t(toSeconds * rate));
    for (std::size_t i = from; i < to; ++i) {
        const double t = double(i) / rate;
        double v = 0.0;
        for (int h = 1; h <= 6; ++h) v += std::sin(2.0 * std::numbers::pi * hz * h * t) / (h * h);
        out[i] += level * float(v);
    }
}

void addChord(std::vector<float>& out, std::initializer_list<int> notes, double from, double to, double a4 = 440.0)
{
    for (int note : notes) addNote(out, hzOf(note, a4), from, to);
}

// Noise bursts (20 ms) four times a second: a drummer over the chords.
void addHits(std::vector<float>& out, float level)
{
    std::uint32_t seed = 99;
    for (double t = 0.1; t < double(out.size()) / rate - 0.05; t += 0.25) {
        const auto at = std::size_t(t * rate);
        for (std::size_t i = 0; i < std::size_t(0.02 * rate); ++i) {
            seed = seed * 1664525u + 1013904223u;
            out[at + i] += level * (float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f);
        }
    }
}

// The three strongest note names of a row, as a sorted text ("C E G").
std::string top3(const Chroma& c)
{
    std::vector<int> order{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    std::sort(order.begin(), order.end(), [&](int a, int b) { return c[std::size_t(a)] > c[std::size_t(b)]; });
    std::sort(order.begin(), order.begin() + 3);
    return std::string(noteName(order[0])) + " " + noteName(order[1]) + " " + noteName(order[2]);
}

int strongest(const Chroma& c) { return int(std::max_element(c.begin(), c.end()) - c.begin()); }

} // namespace

TEST(chromagram, a_chord_shows_its_three_notes_and_its_bass)
{
    // C major (C3 in the bass, C4 E4 G4), then A minor (A2; A3 C4 E4), 3 s each.
    std::vector<float> sound(std::size_t(6.0 * rate), 0.0f);
    addChord(sound, {48, 60, 64, 67}, 0.0, 3.0);
    addChord(sound, {45, 57, 60, 64}, 3.0, 6.0);
    const auto c = chromagram(sound, rate);
    CHECK(c.frames > 100 && std::abs(c.frameRate - 20.0) < 0.01);
    CHECK(c.notes.size() == std::size_t(c.frames) && c.bass.size() == c.notes.size());
    CHECK(std::abs(c.tuningCents) < 5.0);
    const auto first = chromaBetween(c.notes, c.frameRate, 0.5, 2.5), second = chromaBetween(c.notes, c.frameRate, 3.5, 5.5);
    std::printf("    C major: %s; A minor: %s; tuning %.1f cents\n", top3(first).c_str(), top3(second).c_str(), c.tuningCents);
    CHECK(top3(first) == "C E G");
    CHECK(top3(second) == "C E A");
    CHECK(strongest(chromaBetween(c.bass, c.frameRate, 0.5, 2.5)) == 0);     // C
    CHECK(strongest(chromaBetween(c.bass, c.frameRate, 3.5, 5.5)) == 9);     // A
    // Scaled to 1, and the notes not in the chord stay low.
    CHECK(std::abs(*std::max_element(first.begin(), first.end()) - 1.0f) < 1e-6f);
    for (int name : {1, 3, 6, 8, 10}) CHECK(first[std::size_t(name)] < 0.25f);
}

TEST(chromagram, a_song_tuned_sharp_or_flat_is_read_at_its_own_pitch)
{
    for (double a4 : {446.0, 432.0}) {
        std::vector<float> sound(std::size_t(4.0 * rate), 0.0f);
        addChord(sound, {50, 62, 65, 69}, 0.0, 4.0, a4);                    // D minor: D F A
        const auto c = chromagram(sound, rate);
        const double cents = 1200.0 * std::log2(a4 / 440.0);
        std::printf("    A = %.0f Hz: tuning %.1f cents (played %.1f), %s\n", a4, c.tuningCents, cents,
                    top3(chromaBetween(c.notes, c.frameRate, 0.5, 3.5)).c_str());
        CHECK(std::abs(c.tuningCents - cents) < 6.0);
        CHECK(top3(chromaBetween(c.notes, c.frameRate, 0.5, 3.5)) == "D F A");
    }
}

TEST(chromagram, drum_hits_over_the_chord_do_not_change_it)
{
    std::vector<float> sound(std::size_t(5.0 * rate), 0.0f);
    addChord(sound, {43, 55, 59, 62}, 0.0, 5.0);                            // G major: G B D
    addHits(sound, 0.8f);
    const auto c = chromagram(sound, rate);
    const auto row = chromaBetween(c.notes, c.frameRate, 0.5, 4.5);
    std::printf("    G major under hits: %s\n", top3(row).c_str());
    CHECK(top3(row) == "D G B");
    CHECK(strongest(chromaBetween(c.bass, c.frameRate, 0.5, 4.5)) == 7);    // G
}

TEST(chromagram, other_sample_rates_short_silent_and_cancelled_sounds)
{
    // 44.1 kHz reads the same notes.
    {
        const double r = 44100.0;
        std::vector<float> sound(std::size_t(3.0 * r), 0.0f);
        for (int note : {52, 64, 68, 71})                                    // E major: E G# B
            for (std::size_t i = 0; i < sound.size(); ++i)
                for (int h = 1; h <= 4; ++h) sound[i] += 0.15f * float(std::sin(2.0 * std::numbers::pi * hzOf(note) * h * double(i) / r) / (h * h));
        const auto c = chromagram(sound, r);
        CHECK(top3(chromaBetween(c.notes, c.frameRate, 0.5, 2.5)) == "E G# B");
    }
    CHECK(chromagram(std::vector<float>(std::size_t(0.5 * rate), 0.1f), rate).frames == 0);   // under a second
    CHECK(chromagram({}, rate).frames == 0);
    const auto silent = chromagram(std::vector<float>(std::size_t(2.0 * rate), 0.0f), rate);
    CHECK(silent.frames > 0);
    const auto none = chromaBetween(silent.notes, silent.frameRate, 0.0, 2.0);
    CHECK(*std::max_element(none.begin(), none.end()) == 0.0f);
    std::atomic<bool> cancel{true};
    Progress progress;
    progress.cancel = &cancel;
    std::vector<float> sound(std::size_t(3.0 * rate), 0.0f);
    addChord(sound, {48, 60, 64, 67}, 0.0, 3.0);
    CHECK(chromagram(sound, rate, progress).frames == 0);
    // chromaBetween: a span between two frames takes the nearest one; outside the sound, the edge.
    const auto c = chromagram(sound, rate);
    CHECK(strongest(chromaBetween(c.notes, c.frameRate, 1.001, 1.002)) == strongest(chromaBetween(c.notes, c.frameRate, 0.9, 1.1)));
    CHECK(chromaBetween({}, 20.0, 0.0, 1.0) == Chroma{});
    CHECK(std::string(noteName(0)) == "C" && std::string(noteName(13)) == "C#" && std::string(noteName(-1)) == "B");
}
