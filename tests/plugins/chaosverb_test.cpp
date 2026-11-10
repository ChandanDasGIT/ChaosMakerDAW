#include "testkit.h"
#include "plugins/chaosverb/chaosverb.h"

#include <cmath>
#include <random>
#include <vector>

namespace {

constexpr double rate = 48000.0;

struct Stereo { std::vector<float> l, r; };

Stereo run(MixEffect& fx, std::vector<float> x)
{
    Stereo out{x, x};
    for (std::size_t at = 0; at < x.size(); at += 128) {
        const int n = int(std::min<std::size_t>(128, x.size() - at));
        float* channels[]{out.l.data() + at, out.r.data() + at};
        ProcessContext ctx;
        ctx.audio = {channels, 2, n};
        ctx.sampleRate = rate;
        fx.process(ctx);
    }
    return out;
}

std::vector<float> impulse(double seconds)
{
    std::vector<float> x(static_cast<std::size_t>(seconds * rate), 0.0f);
    x[0] = 1.0f;
    return x;
}

double energy(const std::vector<float>& x, double from, double to)
{
    double sum = 0;
    for (std::size_t i = std::size_t(from * rate); i < std::size_t(to * rate) && i < x.size(); ++i) sum += double(x[i]) * x[i];
    return sum;
}
double energyDb(const std::vector<float>& x, double from, double to) { return 10.0 * std::log10(std::max(energy(x, from, to), 1e-30)); }

// How much of the tail's energy is in fast changes (highs), from `from` seconds on.
double brightness(const std::vector<float>& x, double from)
{
    double hi = 0, all = 0;
    for (std::size_t i = std::size_t(from * rate) + 1; i < x.size(); ++i) {
        const double d = x[i] - x[i - 1];
        hi += d * d; all += double(x[i]) * x[i];
    }
    return hi / std::max(all, 1e-30);
}

ChaosVerb& with(ChaosVerb& fx, std::initializer_list<std::pair<const char*, float>> values)
{
    fx.prepare(rate, 128);
    for (const auto& [id, v] : values) fx.parameter(id)->set(v);
    fx.reset();
    return fx;
}

} // namespace

TEST(chaosverb, the_tail_follows_decay)
{
    for (float decay : {1.0f, 3.0f, 8.0f}) {
        ChaosVerb fx;
        const auto out = run(with(fx, {{"mix", 100}, {"decay", decay}, {"earlyLate", 100}, {"bassDecay", 1},
                                       {"trebleDecay", 1}, {"colour", 2}, {"mode", 0}, {"lowCut", 20}, {"highCut", 20000}}),
                             impulse(decay * 0.8 + 0.6));
        const double t0 = 0.3, t1 = 0.3 + decay / 3.0;
        const double drop = energyDb(out.l, t0, t0 + 0.1) - energyDb(out.l, t1, t1 + 0.1);
        CHECK(drop > 12.0 && drop < 28.0);   // about 20 dB per third of the decay
    }
}

TEST(chaosverb, every_mode_and_colour_is_stable_at_the_longest_decay)
{
    std::mt19937 random(11);
    std::normal_distribution<float> noise(0.0f, 0.1f);
    std::vector<float> x(static_cast<std::size_t>(3 * rate));
    for (auto& v : x) v = noise(random);
    for (int mode = 0; mode < 7; ++mode)
        for (int colour = 0; colour < 3; ++colour) {
            ChaosVerb fx;
            const auto out = run(with(fx, {{"mode", float(mode)}, {"colour", float(colour)}, {"mix", 100}, {"decay", 20},
                                           {"size", 100}, {"bassDecay", 2}, {"trebleDecay", 1}, {"modDepth", 100}, {"modRate", 5}}), x);
            float peak = 0;
            bool finite = true;
            for (float v : out.l) { finite &= std::isfinite(v); peak = std::max(peak, std::abs(v)); }
            CHECK(finite);
            CHECK(peak < 5.0f);
            const double rmsDb = 10.0 * std::log10(energy(out.l, 1.5, 3) / (1.5 * rate));
            CHECK(rmsDb > -40.0 && rmsDb < -6.0);
        }
}

TEST(chaosverb, modes_colours_movement_and_early_late_change_the_sound)
{
    // The 1970s colour is darker than Now.
    ChaosVerb seventies, now;
    const double dark = brightness(run(with(seventies, {{"mix", 100}, {"colour", 0}, {"highCut", 20000}}), impulse(1.5)).l, 0.3);
    const double clean = brightness(run(with(now, {{"mix", 100}, {"colour", 2}, {"highCut", 20000}}), impulse(1.5)).l, 0.3);
    CHECK(dark < 0.7 * clean);

    // Treble decay below 1: the highs die sooner than the middle.
    ChaosVerb even, mellow;
    const double evenLate = brightness(run(with(even, {{"mix", 100}, {"trebleDecay", 1}, {"highCut", 20000}, {"decay", 4}}), impulse(2.5)).l, 1.5);
    const double mellowLate = brightness(run(with(mellow, {{"mix", 100}, {"trebleDecay", 0.2f}, {"highCut", 20000}, {"decay", 4}}), impulse(2.5)).l, 1.5);
    CHECK(mellowLate < 0.6 * evenLate);

    // Early / late: all reflections puts the energy in the first 100 ms; all tail, later.
    ChaosVerb early, late;
    const auto e = run(with(early, {{"mix", 100}, {"earlyLate", 0}, {"preDelay", 0}}), impulse(1.0)).l;
    const auto l = run(with(late, {{"mix", 100}, {"earlyLate", 100}, {"preDelay", 0}}), impulse(1.0)).l;
    CHECK(energy(e, 0.2, 1.0) < 0.01 * energy(e, 0.0, 0.2));
    CHECK(energy(l, 0.2, 1.0) > 0.2 * energy(l, 0.0, 0.2));

    // Movement: with depth the tail differs from the same reverb standing still.
    ChaosVerb still, moving;
    const auto a = run(with(still, {{"mix", 100}, {"modDepth", 0}}), impulse(1.0)).l;
    const auto b = run(with(moving, {{"mix", 100}, {"modDepth", 100}, {"modRate", 2}}), impulse(1.0)).l;
    double diff = 0;
    for (std::size_t i = std::size_t(0.3 * rate); i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    CHECK(diff > 1.0);

    // Every mode has its own shape.
    std::vector<double> firstHundredMs;
    for (int mode = 0; mode < 7; ++mode) {
        ChaosVerb fx;
        firstHundredMs.push_back(energyDb(run(with(fx, {{"mix", 100}, {"mode", float(mode)}}), impulse(0.5)).l, 0.0, 0.1));
    }
    for (int i = 0; i < 7; ++i)
        for (int j = i + 1; j < 7; ++j) CHECK(std::abs(firstHundredMs[std::size_t(i)] - firstHundredMs[std::size_t(j)]) > 0.05);
}

TEST(chaosverb, mix_zero_is_the_dry_track_and_pre_delay_holds_the_reverb_back)
{
    ChaosVerb dry;
    const auto out = run(with(dry, {{"mix", 0}}), impulse(0.3));
    CHECK(out.l[0] == 1.0f && energy(out.l, 0.001, 0.3) < 1e-12);
    ChaosVerb held;
    const auto wet = run(with(held, {{"mix", 100}, {"preDelay", 80}}), impulse(0.5));
    CHECK(energy(wet.l, 0.0, 0.075) < 1e-12);
    CHECK(energy(wet.l, 0.08, 0.3) > 1e-4);
}
