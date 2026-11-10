#include "testkit.h"
#include "plugins/compressor/mixcompressor.h"
#include "plugins/dynamics/gaincomputer.h"

#include <cmath>
#include <vector>

namespace {

constexpr double rate = 48000.0;
constexpr float pi = 3.14159265358979f;

struct Result { std::vector<float> left, right; };

// A 1 kHz sine at peakDb for `seconds`, through the compressor in 256-frame blocks.
Result run(MixCompressor& compressor, float peakDb, double seconds = 1.0)
{
    const int total = int(seconds * rate);
    Result r{std::vector<float>(std::size_t(total)), std::vector<float>(std::size_t(total))};
    const float amplitude = dynamics::fromDb(peakDb);
    for (int i = 0; i < total; ++i) r.left[std::size_t(i)] = r.right[std::size_t(i)] = amplitude * std::sin(2 * pi * 1000.0f * float(i) / float(rate));
    for (int at = 0; at < total; at += 256) {
        const int n = std::min(256, total - at);
        float* channels[]{r.left.data() + at, r.right.data() + at};
        ProcessContext ctx;
        ctx.audio = {channels, 2, n};
        ctx.sampleRate = rate;
        compressor.process(ctx);
    }
    return r;
}

// Peak over the last `seconds`, in dB.
float tailPeakDb(const std::vector<float>& x, double seconds = 0.1)
{
    float peak = 0;
    for (std::size_t i = x.size() - std::size_t(seconds * rate); i < x.size(); ++i) peak = std::max(peak, std::abs(x[i]));
    return dynamics::toDb(peak);
}

MixCompressor& set(MixCompressor& c, std::initializer_list<std::pair<const char*, float>> values)
{
    for (const auto& [id, v] : values) c.parameter(id)->set(v);
    c.reset();   // start the smoothers at the new values
    return c;
}

} // namespace

TEST(mixcompressor, the_gain_computer_follows_threshold_ratio_and_knee)
{
    const float s4 = dynamics::feedForwardSlope(4);
    CHECK(dynamics::reductionDb(-30, -18, s4, 0) == 0.0f);
    CHECK_NEAR(dynamics::reductionDb(-6, -18, s4, 0), 9.0f, 1e-5);    // 12 dB over, 3/4 of it
    CHECK(dynamics::reductionDb(-6, -18, dynamics::feedForwardSlope(1), 0) == 0.0f);   // 1:1 does nothing
    // The knee is smooth: continuous at its edges, gentler than hard inside it.
    CHECK_NEAR(dynamics::reductionDb(-21, -18, s4, 6), 0.0f, 1e-6);
    CHECK_NEAR(dynamics::reductionDb(-15, -18, s4, 6), dynamics::reductionDb(-15, -18, s4, 0), 1e-5);
    CHECK(dynamics::reductionDb(-18, -18, s4, 6) > 0.0f);
    // Feedback: the reduction R taken from the output (input - R) lands on the same 9 dB.
    const float r = 9.0f;
    CHECK_NEAR(dynamics::reductionDb(-6 - r, -18, dynamics::feedbackSlope(4), 0), r, 1e-5);
}

TEST(mixcompressor, below_threshold_nothing_changes_and_above_it_the_ratio_holds)
{
    MixCompressor quiet;
    quiet.prepare(rate, 256);
    const auto under = run(quiet, -30);
    CHECK_NEAR(tailPeakDb(under.left), -30.0f, 0.1);
    CHECK(quiet.reductionDb() < 0.01f);

    for (int style : {0, 1, 2}) {
        MixCompressor c;
        c.prepare(rate, 256);
        set(c, {{"style", float(style)}, {"knee", 0}, {"threshold", -18}, {"ratio", 4}});
        const auto out = run(c, -6, 1.5);
        const float peak = tailPeakDb(out.left);
        CHECK(std::isfinite(peak));
        CHECK(peak < -10.0f);                    // clearly turned down from -6
        CHECK_NEAR(peak, -15.0f, 1.5);           // -18 + 12 / 4 in every style
        CHECK(c.reductionDb() > 5.0f);
        CHECK_NEAR(tailPeakDb(out.left), tailPeakDb(out.right), 1e-4);   // stereo-linked
    }
}

TEST(mixcompressor, makeup_mix_and_attack_do_what_they_say)
{
    MixCompressor plain;
    plain.prepare(rate, 256);
    set(plain, {{"knee", 0}});
    const float compressed = tailPeakDb(run(plain, -6).left);

    MixCompressor makeup;
    makeup.prepare(rate, 256);
    set(makeup, {{"knee", 0}, {"makeup", 6}});
    CHECK_NEAR(tailPeakDb(run(makeup, -6).left), compressed + 6.0f, 0.2);

    MixCompressor autoMakeup;
    autoMakeup.prepare(rate, 256);
    set(autoMakeup, {{"knee", 0}, {"autoMakeup", 1}});
    CHECK_NEAR(tailPeakDb(run(autoMakeup, -6).left), compressed + 6.75f, 0.2);   // half of 13.5 dB

    MixCompressor dry;
    dry.prepare(rate, 256);
    set(dry, {{"mix", 0}});
    CHECK_NEAR(tailPeakDb(run(dry, -6).left), -6.0f, 0.05);

    // A fast attack has turned a sudden loud sine down within 2 ms; a slow one has not yet.
    const auto reductionAfter = [](float attackMs) {
        MixCompressor c;
        c.prepare(rate, 96);
        set(c, {{"knee", 0}, {"attack", attackMs}});
        std::vector<float> l(96), r(96);
        for (int i = 0; i < 96; ++i) l[std::size_t(i)] = r[std::size_t(i)] = 0.5f * std::sin(2 * pi * 1000.0f * float(i) / float(rate));
        float* channels[]{l.data(), r.data()};
        ProcessContext ctx;
        ctx.audio = {channels, 2, 96};
        ctx.sampleRate = rate;
        c.process(ctx);
        return c.reductionDb();
    };
    CHECK(reductionAfter(0.1f) > 3.0f * reductionAfter(50.0f));
}

TEST(mixcompressor, bypass_passes_the_signal_and_crossfades_without_a_jump)
{
    MixCompressor c;
    c.prepare(rate, 256);
    set(c, {{"knee", 0}, {"bypass", 1}});
    CHECK_NEAR(tailPeakDb(run(c, -6).left), -6.0f, 0.01);
    CHECK(c.reductionDb() == 0.0f);

    // Switching on mid-signal: the change is spread over 20 ms, no step between samples.
    c.parameter("bypass")->set(0);
    const auto out = run(c, -6, 0.2);
    float biggestStep = 0;
    for (std::size_t i = 1; i < out.left.size(); ++i) biggestStep = std::max(biggestStep, std::abs(out.left[i] - out.left[i - 1]));
    const float sineStep = dynamics::fromDb(-6) * 2 * pi * 1000.0f / float(rate);   // the sine's own steepest step
    CHECK(biggestStep < sineStep * 1.05f);
    CHECK(tailPeakDb(out.left, 0.05) < -10.0f);   // and it compresses after the fade
}
