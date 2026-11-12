#include "testkit.h"
#include "analysis/hpss.h"
#include "analysis/onsets.h"
#include "analysis/spectrogram.h"
#include "drumrender.h"

#include <atomic>
#include <cmath>
#include <numbers>
#include <vector>

using namespace analysis;

namespace {

constexpr double rate = 48000.0;

std::vector<float> sine(double hz, double seconds, float level = 0.5f)
{
    std::vector<float> out(std::size_t(seconds * rate));
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = level * float(std::sin(2.0 * std::numbers::pi * hz * double(i) / rate));
    return out;
}

// Short noise bursts (5 ms) every `every` seconds from `first`.
std::vector<float> clicks(double first, double every, double seconds)
{
    std::vector<float> out(std::size_t(seconds * rate), 0.0f);
    std::uint32_t seed = 12345;
    for (double t = first; t < seconds - 0.01; t += every) {
        const auto at = std::size_t(t * rate);
        for (std::size_t i = 0; i < std::size_t(0.005 * rate) && at + i < out.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            out[at + i] = 0.8f * (float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f);
        }
    }
    return out;
}

double bandEnergy(const BandSpectrogram& s, const std::vector<float>& cells, double fromHz, double toHz)
{
    double sum = 0.0;
    const int from = s.bandAt(fromHz), to = s.bandAt(toHz);
    for (int f = 0; f < s.frames; ++f)
        for (int b = from; b < to; ++b) sum += double(cells[std::size_t(f) * s.bands + b]);
    return sum;
}

} // namespace

TEST(spectrogram, frames_are_ten_ms_and_a_tone_lands_in_its_band)
{
    const auto s = bandSpectrogram(sine(1000.0, 1.0), rate);
    CHECK(s.hop == 480);
    CHECK_NEAR(s.frameRate(), 100.0, 1e-9);
    CHECK(s.frames == 101);
    CHECK(s.bands == int(std::floor(std::log2(16000.0 / 30.0) * 8)));
    int loudest = 0;
    for (int b = 1; b < s.bands; ++b)
        if (s.at(50, b) > s.at(50, loudest)) loudest = b;
    CHECK(s.centreHz[loudest] > 1000.0 / 1.1 && s.centreHz[loudest] < 1000.0 * 1.1);
    // 44.1 kHz: still about 10 ms.
    const auto t = bandSpectrogram(std::vector<float>(44100, 0.1f), 44100.0);
    CHECK(t.hop == 441);
    CHECK(bandSpectrogram({}, rate).frames == 0);
}

TEST(spectrogram, a_cancelled_analysis_returns_nothing)
{
    std::atomic<bool> cancel{true};
    Progress p;
    p.cancel = &cancel;
    CHECK(bandSpectrogram(sine(440.0, 5.0), rate, p).frames == 0);
}

TEST(spectrogram, a_held_tone_is_harmonic_and_clicks_are_percussive)
{
    auto sound = sine(220.0, 4.0, 0.3f);
    const auto c = clicks(0.25, 0.5, 4.0);
    for (std::size_t i = 0; i < sound.size(); ++i) sound[i] += c[i];
    const auto s = bandSpectrogram(sound, rate);
    const auto parts = separate(s);
    CHECK(parts.percussive.size() == s.magnitude.size() && parts.harmonic.size() == s.magnitude.size());
    // Around the tone most is harmonic; high up (only the clicks) most is percussive.
    const double toneH = bandEnergy(s, parts.harmonic, 200.0, 245.0), toneP = bandEnergy(s, parts.percussive, 200.0, 245.0);
    const double highH = bandEnergy(s, parts.harmonic, 4000.0, 12000.0), highP = bandEnergy(s, parts.percussive, 4000.0, 12000.0);
    CHECK(toneH > 4.0 * toneP);
    CHECK(highP > 2.0 * highH);
}

TEST(spectrogram, onsets_find_the_clicks_on_time_and_not_the_tone)
{
    auto sound = sine(330.0, 6.0, 0.3f);
    const auto c = clicks(0.3, 0.4, 6.0);
    for (std::size_t i = 0; i < sound.size(); ++i) sound[i] += c[i];
    const auto s = bandSpectrogram(sound, rate);
    const auto e = envelopes(s, separate(s).percussive);
    const auto onsets = pickOnsets(e);
    int expected = 0, found = 0;
    double worst = 0.0;
    for (double t = 0.3; t < 5.99; t += 0.4) {
        ++expected;
        double nearest = 1e9;
        for (const auto& o : onsets) nearest = std::min(nearest, std::abs(o.seconds - t));
        if (nearest < 0.03) { ++found; worst = std::max(worst, nearest); }
    }
    std::printf("    clicks: %d of %d found, %zu onsets, worst %.1f ms\n", found, expected, onsets.size(), worst * 1000.0);
    CHECK(found == expected);
    CHECK(int(onsets.size()) <= expected + 1);                 // the tone's start at most
    CHECK(worst <= 0.015);
}
