#include "testkit.h"
#include "testsignals.h"
#include "dsp/fft.h"
#include "instruments/guitar/dsp/oversampler2x.h"
#include "instruments/guitar/dsp/oversampler4x.h"

#include <cmath>
#include <complex>
#include <vector>

namespace {
float identity(float x, void*) noexcept { return x; }
float clip(float x, void*) noexcept { return std::tanh(8.0f * x); }

template <typename Oversampler>
std::vector<float> run(std::vector<float> x, float (*shape)(float, void*))
{
    Oversampler o;
    for (auto& v : x) v = o.process(v, 0, shape, nullptr);
    return x;
}

std::vector<float> exactSine(double hz, double gain, int frames)
{
    std::vector<float> x(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) x[std::size_t(i)] = float(gain * std::sin(testsignals::kTwoPi * hz * i / 48000.0));
    return x;
}

// Energy between the harmonics of f0 (aliases folded back), relative to the harmonics.
double aliasesBelowHarmonicsDb(const std::vector<float>& x, double f0)
{
    const int n = 32768; Fft fft(n); std::vector<std::complex<double>> b(n);
    for (int i = 0; i < n; ++i) b[std::size_t(i)] = x[x.size() - n + std::size_t(i)] * (0.5 - 0.5 * std::cos(testsignals::kTwoPi * i / n));
    fft.forward(b.data());
    double harmonic = 0, between = 0;
    for (int k = 1; k < n / 2; ++k) {
        const double hz = k * 48000.0 / n, h = hz / f0;
        const double p = std::norm(b[std::size_t(k)]);
        if (std::abs(h - std::round(h)) * f0 < 8 * 48000.0 / n) harmonic += p; else between += p;
    }
    return 10 * std::log10(harmonic / between);
}
}

TEST(oversampler, delay_is_what_the_pedals_report)
{
    // Delay in base-rate samples, from the phase of a sine sitting exactly on FFT bin 171
    // (about 1 kHz), so the phase reading is exact.
    const double hz = 171 * 48000.0 / 8192;
    std::vector<float> in(16384);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = float(0.5 * std::sin(testsignals::kTwoPi * hz * double(i) / 48000.0));
    const auto delay = [&](const std::vector<float>& out) {
        const int n = 8192; Fft fft(n);
        std::vector<std::complex<double>> a(n), b(n);
        for (int i = 0; i < n; ++i) { a[std::size_t(i)] = in[std::size_t(4096 + i)]; b[std::size_t(i)] = out[std::size_t(4096 + i)]; }
        fft.forward(a.data()); fft.forward(b.data());
        double phase = std::arg(a[171]) - std::arg(b[171]);
        while (phase < 0) phase += testsignals::kTwoPi;
        return phase / (testsignals::kTwoPi * 171 / n);
    };
    CHECK_NEAR(delay(run<Oversampler2x>(in, identity)), 23.0, 0.05);
    CHECK_NEAR(delay(run<Oversampler4x>(in, identity)), 26.5, 0.05);
}

TEST(oversampler, passband_is_flat_through_the_audible_range)
{
    for (double hz : {100.0, 1000.0, 10000.0, 18000.0}) {
        const auto in = exactSine(hz, 0.5, 24000);
        const auto tail = [](const std::vector<float>& v) { return testsignals::rmsDb({v.begin() + 12000, v.end()}); };
        CHECK_NEAR(tail(run<Oversampler2x>(in, identity)), tail(in), 0.05);
        // 4x: the light inner step starts to roll off at the very top (-0.2 dB at 18 kHz); the
        // old filters were ~2 dB down there, and a guitar cab removes that range anyway.
        CHECK_NEAR(tail(run<Oversampler4x>(in, identity)), tail(in), hz > 15000 ? 0.3 : 0.1);
    }
}

TEST(oversampler, hard_clipping_folds_back_little_grit)
{
    // A loud 3.3 kHz tone clipped hard makes harmonics far above 24 kHz; what folds back
    // lands between the harmonics. The steep filters keep it far below them.
    const auto in = exactSine(3317.0, 0.8, 48000);
    CHECK(aliasesBelowHarmonicsDb(run<Oversampler2x>(in, clip), 3317.0) > 40.0);
    CHECK(aliasesBelowHarmonicsDb(run<Oversampler4x>(in, clip), 3317.0) > 50.0);
}
