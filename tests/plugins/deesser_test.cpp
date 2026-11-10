#include "testkit.h"
#include "plugins/deesser/deesser.h"
#include "plugins/dynamics/gaincomputer.h"

#include <cmath>
#include <vector>

namespace {

constexpr double rate = 48000.0;
constexpr float pi = 3.14159265358979f;

// Two sines (a voice-like low tone and a sibilant high one) for one second.
std::vector<float> tones(float lowDb, float highDb)
{
    std::vector<float> x(static_cast<std::size_t>(rate));
    for (std::size_t i = 0; i < x.size(); ++i) {
        const float t = float(i) / float(rate);
        x[i] = dynamics::fromDb(lowDb) * std::sin(2 * pi * 300.0f * t) + dynamics::fromDb(highDb) * std::sin(2 * pi * 8000.0f * t);
    }
    return x;
}

std::vector<float> run(DeEsser& d, std::vector<float> x)
{
    std::vector<float> right = x;
    for (std::size_t at = 0; at < x.size(); at += 256) {
        const int n = int(std::min<std::size_t>(256, x.size() - at));
        float* channels[]{x.data() + at, right.data() + at};
        ProcessContext ctx;
        ctx.audio = {channels, 2, n};
        ctx.sampleRate = rate;
        d.process(ctx);
    }
    return x;
}

// The level of one frequency in the last half second (correlation with a sine and cosine).
float levelDb(const std::vector<float>& x, float hz)
{
    double s = 0, c = 0;
    const std::size_t from = x.size() / 2;
    for (std::size_t i = from; i < x.size(); ++i) {
        const double t = double(i) / rate;
        s += x[i] * std::sin(2 * M_PI * hz * t);
        c += x[i] * std::cos(2 * M_PI * hz * t);
    }
    const double n = double(x.size() - from);
    return dynamics::toDb(float(2.0 * std::sqrt(s * s + c * c) / n));
}

DeEsser& with(DeEsser& d, std::initializer_list<std::pair<const char*, float>> values)
{
    d.prepare(rate, 256);
    for (const auto& [id, v] : values) d.parameter(id)->set(v);
    d.reset();
    return d;
}

} // namespace

TEST(deesser, split_mode_turns_down_only_the_sibilance_and_no_more_than_the_range)
{
    DeEsser d;
    const auto out = run(with(d, {{"threshold", -40}, {"range", 8}}), tones(-12, -12));
    CHECK_NEAR(levelDb(out, 300), -12.0f, 0.3);              // the voice is left alone
    CHECK_NEAR(levelDb(out, 8000), -20.0f, 1.0);             // the "s" is down by the range
    CHECK(d.reductionDb() > 7.0f && d.reductionDb() <= 8.01f);

    DeEsser quiet;   // a soft "s" under the threshold: nothing changes at all
    const auto untouched = run(with(quiet, {{"threshold", -10}}), tones(-12, -30));
    CHECK_NEAR(levelDb(untouched, 8000), -30.0f, 0.1);
    CHECK_NEAR(levelDb(untouched, 300), -12.0f, 0.1);
    CHECK(quiet.reductionDb() < 0.01f);
}

TEST(deesser, a_loud_low_voice_alone_does_not_trigger_it)
{
    DeEsser d;
    const auto out = run(with(d, {{"threshold", -40}}), tones(-3, -120));
    CHECK_NEAR(levelDb(out, 300), -3.0f, 0.2);
    CHECK(d.reductionDb() < 0.5f);
}

TEST(deesser, wide_mode_dips_everything_and_listen_plays_the_band)
{
    DeEsser wide;
    const auto out = run(with(wide, {{"threshold", -40}, {"range", 6}, {"mode", 1}}), tones(-12, -12));
    CHECK_NEAR(levelDb(out, 300), -18.0f, 1.0);
    CHECK_NEAR(levelDb(out, 8000), -18.0f, 1.0);

    DeEsser listen;
    const auto band = run(with(listen, {{"threshold", 0}, {"listen", 1}}), tones(-12, -12));
    CHECK(levelDb(band, 300) < -40.0f);                      // the voice is filtered out
    CHECK_NEAR(levelDb(band, 8000), -13.2f, 0.5);            // the band passes (1.2 dB down: near the corner)
}
