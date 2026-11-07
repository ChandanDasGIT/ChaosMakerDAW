#include "testkit.h"
#include "dsp/fft.h"

#include <cmath>
#include <complex>
#include <vector>

namespace {
// The slow textbook DFT, to check the fast one against.
std::vector<std::complex<double>> dft(const std::vector<std::complex<double>>& x)
{
    const auto n = x.size();
    std::vector<std::complex<double>> out(n);
    for (std::size_t k = 0; k < n; ++k)
        for (std::size_t t = 0; t < n; ++t)
            out[k] += x[t] * std::polar(1.0, -6.283185307179586 * double(k * t) / double(n));
    return out;
}
}

TEST(fft, size_rounds_up_to_a_power_of_two)
{
    CHECK(Fft(1).size() == 2);
    CHECK(Fft(64).size() == 64);
    CHECK(Fft(100).size() == 128);
}

TEST(fft, matches_the_textbook_dft)
{
    for (int n : {2, 8, 64, 256}) {
        std::vector<std::complex<double>> x(static_cast<std::size_t>(n));
        unsigned seed = 12345;
        for (auto& v : x) {
            seed = seed * 1103515245u + 12345u;
            const double re = double((seed >> 8) & 0xffff) / 65535.0 - 0.5;
            seed = seed * 1103515245u + 12345u;
            v = {re, double((seed >> 8) & 0xffff) / 65535.0 - 0.5};
        }
        const auto expected = dft(x);
        Fft(n).forward(x.data());
        double worst = 0.0;
        for (std::size_t k = 0; k < x.size(); ++k) worst = std::max(worst, std::abs(x[k] - expected[k]));
        CHECK(worst < 1e-9);
    }
}

TEST(fft, impulse_is_flat_and_a_sine_lands_in_its_bin)
{
    Fft fft(32);
    std::vector<std::complex<double>> impulse(32);
    impulse[0] = 1.0;
    fft.forward(impulse.data());
    for (const auto& v : impulse) CHECK_NEAR(std::abs(v), 1.0, 1e-12);

    std::vector<std::complex<double>> sine(32);
    for (int t = 0; t < 32; ++t) sine[std::size_t(t)] = std::cos(6.283185307179586 * 5 * t / 32);
    fft.forward(sine.data());
    CHECK_NEAR(std::abs(sine[5]), 16.0, 1e-9);    // n/2 for a unit cosine
    CHECK_NEAR(std::abs(sine[27]), 16.0, 1e-9);   // its mirror
    CHECK_NEAR(std::abs(sine[4]), 0.0, 1e-9);
}
