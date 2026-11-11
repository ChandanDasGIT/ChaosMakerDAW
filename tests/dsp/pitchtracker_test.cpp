#include "testkit.h"
#include "dsp/pitchtracker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

// A voice-like tone: a fundamental with falling harmonics.
std::vector<float> tone(double hz, double seconds, double rate, double level = 0.3)
{
    std::vector<float> out(std::size_t(seconds * rate));
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = double(i) / rate;
        double v = 0.0;
        for (int h = 1; h <= 6; ++h) v += std::sin(2.0 * std::numbers::pi * hz * h * t) / h;
        out[i] = float(level * v / 2.0);
    }
    return out;
}

double medianHz(const std::vector<pitch::Frame>& frames, std::int64_t from, std::int64_t to)
{
    std::vector<double> hz;
    for (const auto& f : frames)
        if (f.sample >= from && f.sample < to && f.hz > 0.0f) hz.push_back(f.hz);
    if (hz.empty()) return 0.0;
    std::sort(hz.begin(), hz.end());
    return hz[hz.size() / 2];
}

} // namespace

TEST(pitchtracker, finds_the_pitch_of_tones_across_the_voice_range)
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (const double hz : {82.4, 110.0, 220.0, 261.63, 440.0, 880.0, 1318.5}) {
            const auto frames = pitch::Tracker::analyse(tone(hz, 0.5, rate), rate);
            const double found = medianHz(frames, std::int64_t(0.1 * rate), std::int64_t(0.4 * rate));
            const double cents = 1200.0 * std::log2(found / hz);
            CHECK(found > 0.0 && std::abs(cents) < 5.0);
        }
    CHECK_NEAR(pitch::midiOf(440.0), 69.0, 1e-12);
    CHECK_NEAR(pitch::midiOf(261.6256), 60.0, 1e-4);
}

TEST(pitchtracker, frames_come_every_ten_ms_at_their_place_and_silence_has_no_pitch)
{
    const double rate = 48000.0;
    std::vector<float> audio(std::size_t(rate), 0.0f);             // 0.5 s silence, 0.5 s A3
    const auto a3 = tone(220.0, 0.5, rate);
    std::copy(a3.begin(), a3.end(), audio.begin() + std::ptrdiff_t(rate / 2));
    const auto frames = pitch::Tracker::analyse(audio, rate);
    CHECK(frames.size() >= 95 && frames.size() <= 101);
    for (std::size_t i = 1; i < frames.size(); ++i) CHECK(frames[i].sample - frames[i - 1].sample == 480);
    for (const auto& f : frames) {
        if (f.sample < std::int64_t(0.45 * rate)) CHECK(f.hz == 0.0f && f.levelDb < -100.0f);
        if (f.sample > std::int64_t(0.56 * rate) && f.sample < std::int64_t(0.95 * rate)) {
            CHECK(std::abs(1200.0 * std::log2(f.hz / 220.0)) < 5.0);
            CHECK(f.clarity > 0.8f);
        }
    }
    // The pitch starts where the tone starts (within two frames).
    std::int64_t first = -1;
    for (const auto& f : frames) if (f.hz > 0.0f) { first = f.sample; break; }
    CHECK(first > std::int64_t(0.48 * rate) && first < std::int64_t(0.53 * rate));
}

TEST(pitchtracker, noise_is_not_a_pitch_and_a_glide_is_followed)
{
    const double rate = 48000.0;
    std::vector<float> noise(std::size_t(rate / 2));
    std::uint32_t seed = 7;
    for (auto& x : noise) { seed = seed * 1664525u + 1013904223u; x = 0.3f * (float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f); }
    int voiced = 0;
    for (const auto& f : pitch::Tracker::analyse(noise, rate)) voiced += f.hz > 0.0f;
    CHECK(voiced == 0);

    // A glide from 200 to 400 Hz over a second: the frames rise with it.
    std::vector<float> glide(static_cast<std::size_t>(rate));
    double phase = 0.0;
    for (std::size_t i = 0; i < glide.size(); ++i) {
        const double hz = 200.0 * std::pow(2.0, double(i) / rate);
        phase += 2.0 * std::numbers::pi * hz / rate;
        glide[i] = float(0.3 * (std::sin(phase) + 0.5 * std::sin(2 * phase)));
    }
    const auto frames = pitch::Tracker::analyse(glide, rate);
    for (const auto& f : frames) {
        if (f.sample < std::int64_t(0.05 * rate) || f.sample > std::int64_t(0.95 * rate)) continue;
        const double expected = 200.0 * std::pow(2.0, double(f.sample) / rate);
        CHECK(f.hz > 0.0f && std::abs(1200.0 * std::log2(f.hz / expected)) < 15.0);
    }
}

TEST(pitchtracker, live_pushes_in_small_blocks_match_the_whole_analysis)
{
    const double rate = 48000.0;
    const auto audio = tone(330.0, 0.4, rate);
    pitch::Tracker live;
    live.prepare(rate);
    std::vector<pitch::Frame> frames;
    for (std::size_t at = 0; at < audio.size(); at += 128)
        live.push(audio.data() + at, int(std::min<std::size_t>(128, audio.size() - at)), [&](const pitch::Frame& f) { frames.push_back(f); });
    auto whole = pitch::Tracker::analyse(audio, rate);
    CHECK(!frames.empty() && frames.size() <= whole.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        CHECK(frames[i].sample == whole[i].sample);
        if (whole[i].hz > 0.0f) CHECK(std::abs(frames[i].hz - whole[i].hz) < 0.01f);
    }
    live.reset();
    int after = 0;
    live.push(audio.data(), 2000, [&](const pitch::Frame&) { ++after; });
    CHECK(after == 0);                                              // a reset waits for a full window again
}

TEST(pitchtracker, clean_up_moves_octave_slips_back_and_drops_blips)
{
    std::vector<pitch::Frame> frames(20);
    for (std::size_t i = 0; i < frames.size(); ++i) { frames[i].sample = std::int64_t(i) * 480; frames[i].hz = 220.0f; }
    frames[8].hz = 440.0f;                                          // one frame an octave up
    frames[12].hz = 110.0f;                                         // one an octave down
    for (std::size_t i = 15; i < 20; ++i) frames[i].hz = 0.0f;
    frames[17].hz = 300.0f;                                         // a lone blip in silence
    pitch::Tracker::cleanUp(frames);
    CHECK(frames[8].hz == 220.0f && frames[12].hz == 220.0f);
    CHECK(frames[17].hz == 0.0f);
    CHECK(frames[3].hz == 220.0f);
}
