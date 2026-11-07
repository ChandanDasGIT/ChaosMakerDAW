#include "testkit.h"
#include "testsignals.h"
#include "plugins/chaoseq/chaoseq.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using testsignals::kSampleRate;
constexpr double kTwoPi = 6.283185307179586;

struct Stereo {
    std::vector<float> left, right;
};

Stereo sine(double hz, int frames, double rightSign = 1.0)
{
    Stereo s{std::vector<float>(std::size_t(frames)), std::vector<float>(std::size_t(frames))};
    for (int i = 0; i < frames; ++i) {
        const float v = float(0.5 * std::sin(kTwoPi * hz * i / kSampleRate));
        s.left[std::size_t(i)] = v;
        s.right[std::size_t(i)] = float(v * rightSign);
    }
    return s;
}

// Runs the EQ in blocks. between(frame) may change settings at block starts.
template <typename Change>
Stereo render(ChaosEq& eq, Stereo s, int blockSize, Change between)
{
    const int total = int(s.left.size());
    for (int done = 0; done < total; done += blockSize) {
        between(done);
        float* channels[2] = {s.left.data() + done, s.right.data() + done};
        ProcessContext context;
        context.audio = AudioView{channels, 2, std::min(blockSize, total - done)};
        context.sampleRate = kSampleRate;
        testsignals::rtProcess(eq, context);
    }
    return s;
}

Stereo render(ChaosEq& eq, Stereo s, int blockSize = 256)
{
    return render(eq, std::move(s), blockSize, [](int) {});
}

ChaosEq& prepared(ChaosEq& eq)
{
    eq.prepare(kSampleRate, 4096);
    return eq;
}

chaoseq::Settings oneBand(eqband::Shape shape, double hz, double gainDb, double q = 1.0)
{
    chaoseq::Settings s;
    auto& b = s.bands[0];
    b.inUse = true;
    b.filter = {shape, hz, gainDb, q, eqband::Slope::Db12};
    return s;
}

// Level change in dB over the last half of the signal (after fades and glides settled).
double tailGainDb(const std::vector<float>& in, const std::vector<float>& out)
{
    const std::size_t from = in.size() / 2;
    std::vector<float> a(in.begin() + long(from), in.end()), b(out.begin() + long(from), out.end());
    return testsignals::rmsDb(b) - testsignals::rmsDb(a);
}

// Largest jump between neighbouring samples: a click shows up as a jump far above the sine's.
double largestStep(const std::vector<float>& s)
{
    double step = 0.0;
    for (std::size_t i = 1; i < s.size(); ++i) step = std::max(step, double(std::abs(s[i] - s[i - 1])));
    return step;
}

} // namespace

TEST(chaoseq, a_new_eq_has_no_bands_and_leaves_audio_untouched)
{
    ChaosEq eq;
    prepared(eq);
    CHECK(chaoseq::bandsInUse(eq.settings()) == 0);
    CHECK(chaoseq::firstFreeBand(eq.settings()) == 0);
    const auto in = sine(440.0, 4800, -1.0);
    const auto out = render(eq, in);
    CHECK(out.left == in.left && out.right == in.right);
}

TEST(chaoseq, settings_round_trip_and_are_sanitised)
{
    ChaosEq eq;
    prepared(eq);
    chaoseq::Settings s;
    s.bands[3] = {true, true, false, chaoseq::Placement::Side,
                  {eqband::Shape::HighShelf, 4000.0, -6.5, 2.0, eqband::Slope::Db24}};
    s.bands[23] = {true, false, true, chaoseq::Placement::Left,
                   {eqband::Shape::Bell, 1e9, 99.0, 0.5, eqband::Slope::Db12}};
    s.outputGainDb = -3.0;
    s.autoGain = true;
    eq.setSettings(s);
    const auto back = eq.settings();
    CHECK(chaoseq::bandsInUse(back) == 2 && chaoseq::firstFreeBand(back) == 0);
    const auto& b3 = back.bands[3];
    CHECK(b3.inUse && b3.bypassed && !b3.solo && b3.placement == chaoseq::Placement::Side);
    CHECK(b3.filter.shape == eqband::Shape::HighShelf && b3.filter.slope == eqband::Slope::Db24);
    CHECK_NEAR(b3.filter.frequencyHz, 4000.0, 1e-3);
    CHECK_NEAR(b3.filter.gainDb, -6.5, 1e-6);
    CHECK_NEAR(b3.filter.q, 2.0, 1e-6);
    const auto& b23 = back.bands[23];
    CHECK_NEAR(b23.filter.frequencyHz, kSampleRate * 0.49, 1.0);   // clamped below Nyquist
    CHECK(b23.filter.gainDb == eqband::MaxGainDb);
    CHECK(chaoseq::soloBand(back) == 23);
    CHECK_NEAR(back.outputGainDb, -3.0, 1e-6);
    CHECK(back.autoGain);
}

TEST(chaoseq, a_band_sounds_as_designed_once_settled)
{
    ChaosEq eq;
    prepared(eq);
    const auto settings = oneBand(eqband::Shape::Bell, 1000.0, 9.0, 2.0);
    eq.setSettings(settings);
    eq.reset();
    const auto in = sine(1000.0, 48000);
    const auto out = render(eq, in);
    CHECK_NEAR(tailGainDb(in.left, out.left), 9.0, 0.05);
    CHECK_NEAR(tailGainDb(in.right, out.right), 9.0, 0.05);

    // A 48 dB/oct low cut, checked against its computed response.
    auto cut = oneBand(eqband::Shape::LowCut, 2000.0, 0.0, eqband::DefaultQ);
    cut.bands[0].filter.slope = eqband::Slope::Db48;
    eq.setSettings(cut);
    eq.reset();
    const auto low = sine(1000.0, 48000);
    const auto cutOut = render(eq, low);
    const double expected = eqband::magnitudeDb(eqband::design(cut.bands[0].filter, kSampleRate), 1000.0, kSampleRate);
    CHECK_NEAR(tailGainDb(low.left, cutOut.left), expected, 0.1);
}

TEST(chaoseq, changes_fade_and_glide_without_clicks)
{
    ChaosEq eq;
    prepared(eq);
    const auto in = sine(200.0, 48000);
    const double sineStep = largestStep(in.left);   // about 0.013 for this sine
    auto loud = oneBand(eqband::Shape::Bell, 200.0, 18.0, 1.0);
    auto shelf = loud;
    shelf.bands[0].filter.shape = eqband::Shape::LowShelf;
    auto bypassed = shelf;
    bypassed.bands[0].bypassed = true;
    auto moved = loud;
    moved.bands[0].filter.frequencyHz = 3000.0;
    moved.bands[0].filter.gainDb = -18.0;
    const auto out = render(eq, in, 128, [&](int frame) {
        if (frame == 4096) eq.setSettings(loud);       // add a band
        if (frame == 12288) eq.setSettings(moved);     // jump frequency and gain
        if (frame == 20480) eq.setSettings(loud);
        if (frame == 28672) eq.setSettings(shelf);     // change shape
        if (frame == 36864) eq.setSettings(bypassed);  // bypass
    });
    // +18 dB makes the sine's own step about 8 times larger; a click would be far bigger.
    CHECK(largestStep(out.left) < sineStep * 8.0 * 1.3);
    // Bypassed: back to the input at the end.
    CHECK_NEAR(tailGainDb(std::vector<float>(in.left.end() - 4800, in.left.end()),
                          std::vector<float>(out.left.end() - 4800, out.left.end())), 0.0, 0.01);
}

TEST(chaoseq, placement_chooses_which_part_of_the_stereo_signal_changes)
{
    const auto run = [](chaoseq::Placement placement, double rightSign) {
        ChaosEq eq;
        prepared(eq);
        auto s = oneBand(eqband::Shape::Bell, 1000.0, 12.0, 1.0);
        s.bands[0].placement = placement;
        eq.setSettings(s);
        eq.reset();
        const auto in = sine(1000.0, 24000, rightSign);
        const auto out = render(eq, in);
        return std::pair{tailGainDb(in.left, out.left), tailGainDb(in.right, out.right)};
    };
    auto [l, r] = run(chaoseq::Placement::Left, 1.0);
    CHECK_NEAR(l, 12.0, 0.05);
    CHECK_NEAR(r, 0.0, 1e-6);
    std::tie(l, r) = run(chaoseq::Placement::Right, 1.0);
    CHECK_NEAR(l, 0.0, 1e-6);
    CHECK_NEAR(r, 12.0, 0.05);
    // Same signal on both sides is all mid and no side.
    std::tie(l, r) = run(chaoseq::Placement::Mid, 1.0);
    CHECK_NEAR(l, 12.0, 0.05);
    CHECK_NEAR(r, 12.0, 0.05);
    std::tie(l, r) = run(chaoseq::Placement::Side, 1.0);
    CHECK_NEAR(l, 0.0, 1e-5);
    CHECK_NEAR(r, 0.0, 1e-5);
    // Opposite signals are all side.
    std::tie(l, r) = run(chaoseq::Placement::Side, -1.0);
    CHECK_NEAR(l, 12.0, 0.05);
    CHECK_NEAR(r, 12.0, 0.05);
}

TEST(chaoseq, solo_plays_only_the_soloed_band_region)
{
    ChaosEq eq;
    prepared(eq);
    auto s = oneBand(eqband::Shape::Bell, 1000.0, 6.0, 4.0);
    s.bands[0].solo = true;
    eq.setSettings(s);
    eq.reset();
    const auto centre = sine(1000.0, 48000);
    const auto far = sine(100.0, 48000);
    CHECK_NEAR(tailGainDb(centre.left, render(eq, centre).left), 0.0, 0.1);
    CHECK(tailGainDb(far.left, render(eq, far).left) < -25.0);

    // A bypassed band cannot be soloed.
    s.bands[0].bypassed = true;
    eq.setSettings(s);
    eq.reset();
    CHECK_NEAR(tailGainDb(far.left, render(eq, far).left), 0.0, 1e-4);
}

TEST(chaoseq, output_gain_and_auto_gain)
{
    ChaosEq eq;
    prepared(eq);
    chaoseq::Settings s;
    s.outputGainDb = -6.0;
    eq.setSettings(s);
    eq.reset();
    const auto in = sine(440.0, 24000);
    CHECK_NEAR(tailGainDb(in.left, render(eq, in).left), -6.0, 1e-3);

    // A broad +12 dB tilt-free boost: auto-gain pulls the output back down.
    auto boost = oneBand(eqband::Shape::Bell, 1000.0, 12.0, 0.3);
    CHECK(chaoseq::autoGainDb(boost) < -3.0);
    boost.autoGain = true;
    eq.setSettings(boost);
    eq.reset();
    const double compensated = tailGainDb(in.left, render(eq, in).left);
    boost.autoGain = false;
    eq.setSettings(boost);
    eq.reset();
    const double plain = tailGainDb(in.left, render(eq, in).left);
    CHECK_NEAR(compensated, plain + chaoseq::autoGainDb(boost), 0.01);

    // Cuts do not count towards auto-gain.
    CHECK(chaoseq::autoGainDb(oneBand(eqband::Shape::LowCut, 500.0, 0.0)) == 0.0);
}

TEST(chaoseq, analyzer_receives_the_output_only_while_listening)
{
    ChaosEq eq;
    prepared(eq);
    chaoseq::Settings s;
    s.outputGainDb = -6.0;
    eq.setSettings(s);
    eq.reset();
    const auto in = sine(500.0, 1024, -1.0);   // opposite sides: the mono mix is silent
    std::vector<float> got(4096);
    render(eq, in);
    CHECK(eq.readAnalyzer(got) == 0);          // nobody listening: nothing copied

    eq.setAnalyzerListening(true);
    const auto mono = sine(500.0, 1024);
    const auto out = render(eq, mono);
    CHECK(eq.readAnalyzer(got) == 1024);
    for (std::size_t i = 0; i < 1024; ++i) CHECK(got[i] == out.left[i]);
    CHECK(eq.readAnalyzer(got) == 0);          // taken out once

    // Nobody reading: the ring fills and further output is dropped, never blocking.
    for (int i = 0; i < 40; ++i) render(eq, mono);
    CHECK(eq.readAnalyzer(got) == 4096);
}

TEST(chaoseq, block_size_does_not_change_the_result)
{
    auto s = oneBand(eqband::Shape::HighShelf, 3000.0, -8.0, eqband::DefaultQ);
    s.bands[1] = {true, false, false, chaoseq::Placement::Mid, {eqband::Shape::Bell, 300.0, 5.0, 3.0, eqband::Slope::Db12}};
    ChaosEq a, b;
    prepared(a).setSettings(s);
    prepared(b).setSettings(s);
    a.reset();
    b.reset();
    const auto in = sine(250.0, 9600, -0.5);
    const auto big = render(a, in, 512);
    const auto odd = render(b, in, 37);
    double worst = 0.0;
    for (std::size_t i = 0; i < in.left.size(); ++i)
        worst = std::max({worst, double(std::abs(big.left[i] - odd.left[i])), double(std::abs(big.right[i] - odd.right[i]))});
    CHECK(worst < 1e-6);
}
