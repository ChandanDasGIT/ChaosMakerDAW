#include "testkit.h"
#include "plugins/denoise/denoise.h"
#include "persistence/mixfxjson.h"

#include <cmath>
#include <cstdint>
#include <vector>

// Noise reduction (restMap G13).
namespace {

constexpr double rate = 48000.0;

std::vector<float> noise(std::size_t frames, float amplitude, std::uint32_t seed = 12345)
{
    std::vector<float> x(frames);
    for (auto& v : x) {
        seed = seed * 1664525u + 1013904223u;
        v = amplitude * (float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f);
    }
    return x;
}

std::vector<float> tone(std::size_t frames, float hz, float amplitude)
{
    std::vector<float> x(frames);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = amplitude * float(std::sin(6.283185307179586 * hz * double(i) / rate));
    return x;
}

std::vector<float> run(Denoise& fx, std::vector<float> x)
{
    std::vector<float> r = x;
    for (std::size_t at = 0; at < x.size(); at += 128) {
        const int n = int(std::min<std::size_t>(128, x.size() - at));
        float* channels[]{x.data() + at, r.data() + at};
        ProcessContext ctx;
        ctx.audio = {channels, 2, n};
        ctx.sampleRate = rate;
        fx.process(ctx);
    }
    return x;   // the left channel
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) sum += double(x[i]) * x[i];
    return std::sqrt(sum / double(to - from));
}

double db(double ratio) { return 20.0 * std::log10(ratio); }

mixfx::Settings learned(const std::vector<float>& noiseOnly, std::initializer_list<std::pair<const char*, float>> values = {})
{
    auto settings = mixfx::defaults(mixfx::Denoise);
    for (const auto& [id, v] : values) settings.values[id] = v;
    settings.profile = Denoise::learnProfile(noiseOnly);
    return settings;
}

Denoise& prepared(Denoise& fx, const mixfx::Settings& settings)
{
    fx.prepare(rate, 128);
    fx.apply(settings);
    fx.reset();
    return fx;
}

} // namespace

TEST(denoise, without_a_profile_it_passes_the_sound_through_delayed_by_its_latency)
{
    Denoise fx;
    prepared(fx, mixfx::defaults(mixfx::Denoise));
    CHECK(fx.latencySamples() == Denoise::Size);
    std::vector<float> x(8192, 0.0f);
    x[100] = 1.0f;
    const auto y = run(fx, x);
    for (std::size_t i = 0; i < y.size(); ++i) CHECK_NEAR(y[i], i == 100 + Denoise::Size ? 1.0 : 0.0, 1e-4);
    // Any sound: the same, Size samples later.
    Denoise again;
    prepared(again, mixfx::defaults(mixfx::Denoise));
    const auto music = tone(16384, 440.0f, 0.5f);
    const auto out = run(again, music);
    for (std::size_t i = Denoise::Size; i < out.size(); ++i) CHECK_NEAR(out[i], music[i - Denoise::Size], 1e-4);
}

TEST(denoise, learned_noise_drops_by_the_reduction_and_a_tone_above_it_stays)
{
    const auto hiss = noise(std::size_t(3 * rate), 0.05f);
    const auto profile = Denoise::learnProfile(std::vector<float>(hiss.begin(), hiss.begin() + std::ptrdiff_t(rate)));
    CHECK(profile.size() == std::size_t(Denoise::Bins));
    CHECK(Denoise::learnProfile(std::vector<float>(100, 0.1f)).empty());   // too short

    Denoise fx;
    prepared(fx, learned(std::vector<float>(hiss.begin(), hiss.begin() + std::ptrdiff_t(rate)), {{"reduction", 20}}));
    const auto quiet = run(fx, hiss);
    const double drop = db(rms(quiet, std::size_t(rate), hiss.size()) / rms(hiss, std::size_t(rate), hiss.size()));
    CHECK(drop < -14.0 && drop > -22.0);
    CHECK(fx.reductionDb() > 10.0f);

    // A tone far above the noise passes within 1 dB.
    Denoise keep;
    prepared(keep, learned(std::vector<float>(hiss.begin(), hiss.begin() + std::ptrdiff_t(rate)), {{"reduction", 20}}));
    const auto music = tone(std::size_t(2 * rate), 1000.0f, 0.5f);
    const auto out = run(keep, music);
    const double kept = db(rms(out, std::size_t(rate), music.size()) / rms(music, std::size_t(rate) - Denoise::Size, music.size() - Denoise::Size));
    CHECK(std::abs(kept) < 1.0);

    // "Hear removed noise": the noise, nearly all of it.
    Denoise listen;
    prepared(listen, learned(std::vector<float>(hiss.begin(), hiss.begin() + std::ptrdiff_t(rate)), {{"reduction", 20}, {"listen", 1}}));
    const auto removed = run(listen, hiss);
    CHECK(std::abs(db(rms(removed, std::size_t(rate), hiss.size()) / rms(hiss, std::size_t(rate), hiss.size()))) < 3.0);
}

TEST(denoise, bypassed_it_still_delays_so_the_track_stays_in_time)
{
    Denoise fx;
    auto settings = learned(noise(std::size_t(rate), 0.05f));
    settings.bypassed = true;
    prepared(fx, settings);
    const auto music = tone(16384, 300.0f, 0.4f);
    const auto out = run(fx, music);
    for (std::size_t i = Denoise::Size; i < out.size(); ++i) CHECK_NEAR(out[i], music[i - Denoise::Size], 1e-6);
}

TEST(denoise, the_profile_is_kept_with_the_settings_and_checked)
{
    auto settings = learned(noise(std::size_t(rate), 0.05f));
    CHECK(mixfx::sanitize(mixfx::Denoise, settings).profile == settings.profile);
    auto wrong = settings;
    wrong.profile.pop_back();
    CHECK(mixfx::sanitize(mixfx::Denoise, wrong).profile.empty());
    wrong = settings;
    wrong.profile[3] = -1.0f;
    CHECK(mixfx::sanitize(mixfx::Denoise, wrong).profile.empty());
    CHECK(mixfx::sanitize(mixfx::Gate, settings).profile.empty());   // only noise reduction has one

    mixfx::Settings back;
    std::string error;
    CHECK(mixfxjson::fromJson(mixfx::Denoise, mixfxjson::toJson(settings), back, error));
    CHECK(back.profile.size() == settings.profile.size());
    for (std::size_t k = 0; k < back.profile.size(); ++k) CHECK_NEAR(back.profile[k], settings.profile[k], 1e-6);
    auto text = mixfxjson::toJson(settings);
    text["profile"][0] = "loud";
    CHECK(!mixfxjson::fromJson(mixfx::Denoise, text, back, error));
    CHECK(!mixfxjson::toJson(mixfx::defaults(mixfx::Gate)).contains("profile"));
}
