#include "testkit.h"
#include "plugins/delay/mixdelay.h"
#include "plugins/dynamics/gaincomputer.h"
#include "plugins/gate/gate.h"
#include "plugins/limiter/limiter.h"
#include "plugins/saturation/saturation.h"

#include <cmath>
#include <vector>

// The G7 extras: delay, limiter, saturation and gate.
namespace {

constexpr double rate = 48000.0;
constexpr float pi = 3.14159265358979f;

struct Stereo { std::vector<float> l, r; };

Stereo run(MixEffect& fx, std::vector<float> l, std::vector<float> r, double bpm = 120.0)
{
    Stereo out{std::move(l), std::move(r)};
    for (std::size_t at = 0; at < out.l.size(); at += 128) {
        const int n = int(std::min<std::size_t>(128, out.l.size() - at));
        float* channels[]{out.l.data() + at, out.r.data() + at};
        ProcessContext ctx;
        ctx.audio = {channels, 2, n};
        ctx.sampleRate = rate;
        ctx.bpm = bpm;
        fx.process(ctx);
    }
    return out;
}
Stereo run(MixEffect& fx, const std::vector<float>& x, double bpm = 120.0) { return run(fx, x, x, bpm); }

template <class T>
T& with(T& fx, std::initializer_list<std::pair<const char*, float>> values)
{
    fx.prepare(rate, 128);
    for (const auto& [id, v] : values) fx.parameter(id)->set(v);
    fx.reset();
    return fx;
}

std::vector<float> impulse(double seconds)
{
    std::vector<float> x(static_cast<std::size_t>(seconds * rate), 0.0f);
    x[0] = 1.0f;
    return x;
}

std::vector<float> sine(float hz, float amplitude, double seconds)
{
    std::vector<float> x(static_cast<std::size_t>(seconds * rate));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = amplitude * std::sin(2 * pi * hz * float(i) / float(rate));
    return x;
}

std::size_t loudest(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    std::size_t best = from;
    for (std::size_t i = from; i < to && i < x.size(); ++i) if (std::abs(x[i]) > std::abs(x[best])) best = i;
    return best;
}

float peakOf(const std::vector<float>& x, std::size_t from = 0)
{
    float p = 0;
    for (std::size_t i = from; i < x.size(); ++i) p = std::max(p, std::abs(x[i]));
    return p;
}

// The level of one frequency over the second half (correlation with a sine and a cosine).
float levelAt(const std::vector<float>& x, float hz)
{
    double s = 0, c = 0;
    const std::size_t from = x.size() / 2;
    for (std::size_t i = from; i < x.size(); ++i) {
        const double t = double(i) / rate;
        s += x[i] * std::sin(2 * M_PI * hz * t);
        c += x[i] * std::cos(2 * M_PI * hz * t);
    }
    return float(2.0 * std::sqrt(s * s + c * c) / double(x.size() - from));
}

} // namespace

TEST(mixextras, the_delay_repeats_on_the_beat_or_in_ms_and_fades)
{
    CHECK_NEAR(MixDelay::divisionMs(1, 120), 500.0, 1e-9);      // a quarter at 120 BPM
    CHECK_NEAR(MixDelay::divisionMs(4, 120), 375.0, 1e-9);      // a dotted eighth
    CHECK_NEAR(MixDelay::divisionMs(5, 90), 2000.0 / 9.0, 1e-6);   // an eighth triplet at 90: a third of 666.7 ms
    CHECK(MixDelay::divisionMs(0, 1) <= 4000.0);                  // capped at the line's length

    MixDelay synced;
    const auto out = run(with(synced, {{"mix", 100}, {"feedback", 50}, {"lowCut", 20}, {"highCut", 20000}}), impulse(1.6), 120.0);
    const std::size_t first = loudest(out.l, 1000, 30000), second = loudest(out.l, 30000, 54000);
    CHECK(std::abs(int(first) - 24000) <= 2);                    // 500 ms
    CHECK(std::abs(int(second) - 48000) <= 2);                   // and again
    CHECK(std::abs(out.l[second]) < 0.6f * std::abs(out.l[first]));   // each repeat quieter

    MixDelay free;
    const auto ms = run(with(free, {{"sync", 0}, {"time", 100}, {"mix", 100}}), impulse(0.3));
    CHECK(std::abs(int(loudest(ms.l, 100, 14000)) - 4800) <= 2);

    MixDelay bounce;   // ping-pong: first echo left, the next one right
    const auto pp = run(with(bounce, {{"sync", 0}, {"time", 100}, {"pingPong", 1}, {"mix", 100}, {"feedback", 60},
                                      {"lowCut", 20}, {"highCut", 20000}}), impulse(0.35));
    CHECK(std::abs(pp.l[4800]) > 0.1f && std::abs(pp.r[4800]) < 1e-4f);
    CHECK(std::abs(pp.r[9600]) > 0.05f && std::abs(pp.l[9600]) < 1e-4f);

    MixDelay dry;
    const auto none = run(with(dry, {{"mix", 0}}), impulse(0.6));
    CHECK(none.l[0] == 1.0f && peakOf(none.l, 1) < 1e-6f);

    MixDelay ringing;   // the most feedback still dies away
    const auto tail = run(with(ringing, {{"feedback", 95}, {"sync", 0}, {"time", 50}, {"mix", 100}}), impulse(20.0));
    CHECK(peakOf(tail.l, std::size_t(18 * rate)) < peakOf(tail.l, 1000) * 0.1f);
}

TEST(mixextras, the_limiter_never_goes_over_the_ceiling)
{
    // A sudden loud burst after quiet: nothing over -1 dB, and the quiet part untouched.
    std::vector<float> x = sine(1000, 0.1f, 1.0);
    for (std::size_t i = 24000; i < x.size(); ++i) x[i] = 1.8f * std::sin(2 * pi * 1000.0f * float(i) / float(rate));
    Limiter lim;
    const auto out = run(with(lim, {{"ceiling", -1}, {"gain", 0}}), x);
    CHECK(peakOf(out.l) <= dynamics::fromDb(-1.0f) + 1e-6f);
    CHECK(lim.reductionDb() > 4.0f);
    const int lag = lim.latencySamples();
    CHECK(lag >= 40 && lag <= 56);
    for (std::size_t i = 2000; i < 20000; ++i) CHECK(std::abs(out.l[i] - x[i - std::size_t(lag)]) < 1e-5f);   // untouched, 1 ms late
    // Steady loud sine: held just under the ceiling, not crushed far below it.
    CHECK(peakOf(out.l, 40000) > dynamics::fromDb(-2.0f));
    // Gain pushes a quiet track up into the ceiling.
    Limiter push;
    const auto louder = run(with(push, {{"gain", 24}, {"ceiling", -3}}), sine(200, 0.2f, 1.0));
    CHECK(peakOf(louder.l, 24000) > dynamics::fromDb(-4.0f) && peakOf(louder.l) <= dynamics::fromDb(-3.0f) + 1e-6f);
}

TEST(mixextras, saturation_adds_harmonics_keeps_the_level_and_lines_up_with_the_dry_track)
{
    CHECK(Saturation::shape(0, 0.0f) == 0.0f && std::abs(Saturation::shape(1, 0.0f)) < 1e-6f && Saturation::shape(2, 0.0f) == 0.0f);
    for (int type : {0, 1, 2})
        for (float x = -10; x <= 10; x += 0.01f) CHECK(std::abs(Saturation::shape(type, x)) < 1.6f);

    const auto in = sine(200, 0.5f, 1.0);
    std::vector<float> h2, h3;
    for (int type : {0, 1, 2}) {
        Saturation fx;
        const auto out = run(with(fx, {{"type", float(type)}, {"drive", 18}}), in);
        CHECK(levelAt(out.l, 600) > 0.01f);                       // a 3rd harmonic appears
        CHECK(std::abs(dynamics::toDb(levelAt(out.l, 200)) - dynamics::toDb(0.5f)) < 6.0f);   // about the same level
        h2.push_back(levelAt(out.l, 400));
    }
    CHECK(h2[1] > 5.0f * h2[0]);                                  // Tube adds even harmonics, Tape hardly any

    // Mix 50 % with no drive: dry and wet are aligned, so a high tone does not cancel.
    Saturation clean;
    const auto high = sine(9000, 0.1f, 0.5);
    const auto blend = run(with(clean, {{"drive", 0}, {"mix", 50}, {"type", 0}}), high);
    CHECK(std::abs(dynamics::toDb(levelAt(blend.l, 9000)) - dynamics::toDb(levelAt(high, 9000) * dynamics::fromDb(-0.75f * 0.0f))) < 1.5f);

    // Tone: darker below 50 %, brighter above.
    const auto withTone = [&](float tone) {
        Saturation fx;
        const auto out = run(with(fx, {{"drive", 6}, {"tone", tone}}), sine(5000, 0.3f, 0.5));
        return levelAt(out.l, 5000);
    };
    CHECK(withTone(0) < 0.6f * withTone(50));                     // about 6 dB darker
    CHECK(withTone(100) > 1.6f * withTone(50));                   // about 6 dB brighter
}

TEST(mixextras, the_gate_closes_on_noise_opens_on_the_voice_and_holds)
{
    // Quiet noise-like hiss (-60 dB), a loud phrase, then hiss again.
    std::vector<float> x(std::size_t(1.5 * rate));
    for (std::size_t i = 0; i < x.size(); ++i) {
        const float t = float(i) / float(rate);
        const bool phrase = t >= 0.5f && t < 1.0f;
        x[i] = (phrase ? 0.5f : 0.001f) * std::sin(2 * pi * 440.0f * t);
    }
    Gate gate;
    const auto out = run(with(gate, {{"threshold", -40}, {"range", 40}, {"hold", 20}, {"release", 50}}), x);
    CHECK(peakOf(std::vector<float>(out.l.begin() + 1000, out.l.begin() + 23000)) < 0.001f * 0.012f);   // hiss down 40 dB
    CHECK(peakOf(std::vector<float>(out.l.begin() + 26000, out.l.begin() + 47000)) > 0.49f);           // the phrase passes
    CHECK(peakOf(std::vector<float>(out.l.begin() + 60000, out.l.end())) < 0.001f * 0.02f);             // closed again
    CHECK(gate.reductionDb() > 30.0f);

    Gate shallow;   // Range 10 dB: the hiss is only turned down a little
    const auto soft = run(with(shallow, {{"threshold", -40}, {"range", 10}}), x);
    CHECK(std::abs(dynamics::toDb(peakOf(std::vector<float>(soft.l.begin() + 10000, soft.l.begin() + 23000))) - (dynamics::toDb(0.001f) - 10.0f)) < 1.0f);
}
