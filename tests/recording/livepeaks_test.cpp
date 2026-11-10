#include "testkit.h"
#include "recording/livepeaks.h"

#include <cmath>
#include <limits>
#include <thread>
#include <vector>

TEST(livepeaks, the_summary_covers_everything_so_far_at_any_width)
{
    LivePeaks peaks;
    CHECK(peaks.summary(10).empty());
    // 1000 mono frames: a ramp from -1 to just under 1.
    std::vector<float> ramp(1000);
    for (std::size_t i = 0; i < ramp.size(); ++i) ramp[i] = -1.0f + 2.0f * float(i) / 1000.0f;
    peaks.append(ramp.data(), ramp.size(), 1);
    CHECK(peaks.frames() == 1000);
    const auto one = peaks.summary(1);
    CHECK(one.size() == 1);
    CHECK(one[0].first == -1.0f && std::abs(one[0].second - ramp.back()) < 1e-6f);   // the unfinished last group counts
    const auto four = peaks.summary(4);
    CHECK(four.size() == 4);
    for (std::size_t i = 1; i < four.size(); ++i) CHECK(four[i].first > four[i - 1].first);   // in time order
    CHECK(peaks.summary(100).size() == 100);                 // more bins than groups: groups repeat
    CHECK(peaks.summary(0).empty());

    // A range: the second half only.
    const auto half = peaks.summary(2, 0.5, 1.0);
    CHECK(half.size() == 2 && half[0].first > -0.1f);
    CHECK(peaks.summary(2, 0.7, 0.7).empty());
    CHECK(peaks.summary(2, std::nan(""), 1).empty());
}

TEST(livepeaks, stereo_uses_both_channels_and_bad_samples_are_silence)
{
    LivePeaks peaks;
    std::vector<float> frames(2 * 256);
    for (std::size_t f = 0; f < 256; ++f) { frames[2 * f] = 0.25f; frames[2 * f + 1] = -0.5f; }
    frames[10] = std::numeric_limits<float>::infinity();
    peaks.append(frames.data(), 256, 2);
    const auto s = peaks.summary(1);
    CHECK(s.size() == 1 && s[0].first == -0.5f && s[0].second == 0.25f);
    CHECK(peaks.frames() == 256);
}

TEST(livepeaks, reading_while_writing_is_safe)
{
    LivePeaks peaks;
    std::vector<float> block(128, 0.5f);
    std::thread writer([&] { for (int i = 0; i < 2000; ++i) peaks.append(block.data(), block.size(), 1); });
    std::size_t reads = 0;
    while (peaks.frames() < 2000 * 128) {
        const auto s = peaks.summary(50);
        for (const auto& p : s) CHECK(p.second == 0.5f);
        ++reads;
    }
    writer.join();
    CHECK(reads > 0);
    CHECK(peaks.frames() == 2000 * 128);
}
