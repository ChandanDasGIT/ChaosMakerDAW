#include "testkit.h"
#include "plugins/reverb/roomreverb.h"
#include "plugins/dynamics/gaincomputer.h"

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

double energyDb(const std::vector<float>& x, double from, double to)
{
    return 10.0 * std::log10(std::max(energy(x, from, to), 1e-30));
}

RoomReverb& with(RoomReverb& fx, std::initializer_list<std::pair<const char*, float>> values)
{
    fx.prepare(rate, 128);
    for (const auto& [id, v] : values) fx.parameter(id)->set(v);
    fx.reset();
    return fx;
}

} // namespace

TEST(roomreverb, the_tail_lasts_about_as_long_as_decay_says)
{
    for (float decay : {0.8f, 2.0f, 5.0f}) {
        RoomReverb fx;
        const auto out = run(with(fx, {{"mix", 100}, {"decay", decay}, {"damping", 0}, {"lowCut", 20}, {"highCut", 20000}}), impulse(decay * 1.2 + 0.5));
        // 50 ms windows a third of the decay apart: 20 dB lower per third (60 dB per decay).
        const double t0 = 0.15, t1 = 0.15 + decay / 3.0;
        const double drop = energyDb(out.l, t0, t0 + 0.05) - energyDb(out.l, t1, t1 + 0.05);
        CHECK(drop > 13.0 && drop < 27.0);
    }
}

TEST(roomreverb, pre_delay_mix_width_and_damping_do_what_they_say)
{
    RoomReverb wet;
    const auto out = run(with(wet, {{"mix", 100}, {"preDelay", 50}, {"type", 1}}), impulse(0.5));
    CHECK(energy(out.l, 0.0, 0.045) < 1e-12);                 // nothing before the pre-delay
    CHECK(energy(out.l, 0.05, 0.2) > 1e-4);

    RoomReverb dry;
    const auto unchanged = run(with(dry, {{"mix", 0}}), impulse(0.3));
    CHECK(unchanged.l[0] == 1.0f);
    CHECK(energy(unchanged.l, 0.001, 0.3) < 1e-12);

    RoomReverb mono;
    const auto narrow = run(with(mono, {{"mix", 100}, {"width", 0}}), impulse(0.5));
    double diff = 0;
    for (std::size_t i = 0; i < narrow.l.size(); ++i) diff = std::max(diff, double(std::abs(narrow.l[i] - narrow.r[i])));
    CHECK(diff < 1e-6);
    RoomReverb stereo;
    const auto wide = run(with(stereo, {{"mix", 100}, {"width", 100}}), impulse(0.5));
    double corr = 0, el = 0, er = 0;
    for (std::size_t i = 0; i < wide.l.size(); ++i) { corr += wide.l[i] * wide.r[i]; el += wide.l[i] * wide.l[i]; er += wide.r[i] * wide.r[i]; }
    CHECK(std::abs(corr / std::sqrt(el * er)) < 0.5);           // the two sides differ

    // More damping: the tail loses its highs (less energy in the sample-to-sample change).
    const auto brightness = [](float damping) {
        RoomReverb fx;
        const auto out = run(with(fx, {{"mix", 100}, {"damping", damping}, {"highCut", 20000}, {"decay", 3}}), impulse(1.5));
        double hi = 0, all = 0;
        for (std::size_t i = std::size_t(0.5 * rate) + 1; i < out.l.size(); ++i) {
            const double d = out.l[i] - out.l[i - 1];
            hi += d * d; all += double(out.l[i]) * out.l[i];
        }
        return hi / all;
    };
    CHECK(brightness(90) < 0.5 * brightness(0));
}

TEST(roomreverb, every_type_is_stable_and_sounds_different)
{
    std::mt19937 random(7);
    std::normal_distribution<float> noise(0.0f, 0.1f);
    std::vector<float> x(static_cast<std::size_t>(4 * rate));
    for (auto& v : x) v = noise(random);
    std::vector<double> tails;
    for (int type : {0, 1, 2}) {
        RoomReverb fx;
        const auto out = run(with(fx, {{"type", float(type)}, {"mix", 100}, {"decay", 10}, {"size", 100}, {"damping", 0}}), x);
        float peak = 0;
        for (float v : out.l) { CHECK(std::isfinite(v)); peak = std::max(peak, std::abs(v)); }
        CHECK(peak < 5.0f);                                        // no runaway at the longest decay
        // Loud enough to hear next to the dry signal (noise RMS 0.1 = -20 dBFS).
        const double rmsDb = 10.0 * std::log10(energy(out.l, 2, 4) / (2 * rate));
        CHECK(rmsDb > -32.0 && rmsDb < -8.0);
        RoomReverb shortOne;
        tails.push_back(energyDb(run(with(shortOne, {{"type", float(type)}, {"mix", 100}}), impulse(0.4)).l, 0.0, 0.03));
    }
    CHECK(std::abs(tails[0] - tails[1]) > 0.5 || std::abs(tails[1] - tails[2]) > 0.5);
}
