#include "testkit.h"
#include "dsp/latencymeasure.h"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace {

constexpr double kRate = 48000.0;

// `seconds` of noise at `noise` peak level, with the click added at `delay` times `gain`.
std::vector<float> recording(const std::vector<float>& click, std::int64_t delay, float gain, float noise,
                             double seconds = 0.5, unsigned seed = 7)
{
    std::vector<float> out(static_cast<std::size_t>(kRate * seconds));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-noise, noise);
    for (auto& s : out) s = noise > 0.0f ? dist(rng) : 0.0f;
    for (std::size_t i = 0; i < click.size() && delay + std::int64_t(i) < std::int64_t(out.size()); ++i)
        if (delay >= 0) out[static_cast<std::size_t>(delay) + i] += gain * click[i];
    return out;
}

}

TEST(latencymeasure, the_click_is_short_and_not_silent)
{
    const auto click = latencymeasure::makeClick(kRate);
    CHECK(click.size() == 130);                                  // 2.7 ms at 48 kHz
    float peak = 0.0f;
    for (float s : click) peak = std::max(peak, std::abs(s));
    CHECK(peak > 0.5f && peak <= 0.8f);
    CHECK(std::abs(click.front()) < 1e-6f && std::abs(click.back()) < 1e-6f);   // windowed
}

TEST(latencymeasure, finds_a_known_delay_exactly_through_gain_and_noise)
{
    const auto click = latencymeasure::makeClick(kRate);
    for (std::int64_t delay : {0, 1, 64, 1234, 9001}) {
        const auto r = latencymeasure::findDelay(click, recording(click, delay, 0.3f, 0.01f));
        CHECK(r.found);
        CHECK(r.delaySamples == delay);
        CHECK(!r.inverted);
        CHECK(r.confidence > 0.9);
    }
}

TEST(latencymeasure, an_inverted_loopback_is_found_and_reported)
{
    const auto click = latencymeasure::makeClick(kRate);
    const auto r = latencymeasure::findDelay(click, recording(click, 777, -0.5f, 0.005f));
    CHECK(r.found);
    CHECK(r.delaySamples == 777);
    CHECK(r.inverted);
}

TEST(latencymeasure, nothing_is_found_in_noise_silence_or_past_the_limit)
{
    const auto click = latencymeasure::makeClick(kRate);
    CHECK(!latencymeasure::findDelay(click, recording(click, -1, 0.0f, 0.2f)).found);   // noise only
    CHECK(!latencymeasure::findDelay(click, recording(click, -1, 0.0f, 0.0f)).found);   // silence
    // The click is at 5000, but only delays up to 4000 are searched
    CHECK(!latencymeasure::findDelay(click, recording(click, 5000, 0.5f, 0.01f), 4000).found);
    // Too short a recording, or no click
    CHECK(!latencymeasure::findDelay(click, std::vector<float>(10)).found);
    CHECK(!latencymeasure::findDelay({}, recording(click, 10, 0.5f, 0.0f)).found);
}
