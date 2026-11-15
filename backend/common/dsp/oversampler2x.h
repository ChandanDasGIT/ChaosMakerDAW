#pragma once

#include <array>
#include <cstddef>

// 2x half-band up/downsampler around a nonlinear function (clipping). State is fixed-size and
// all work is stack/register-only in process().
//
// Half-band FIR filters: every other tap except the centre is zero, so those taps are skipped,
// and the history is a ring buffer stored twice so each window is contiguous.
// Two filter sets (plan 6.3 follow-up):
//  - Steep, 47 taps (Kaiser window, beta 8): flat to 0.2 fs, >= 56 dB down from 0.3 fs.
//    Used wherever the base rate meets the oversampled rate, because aliases folding back into
//    the audible band come from there. Latency: 23 samples at the base rate.
//  - Light, 15 taps: for the inner step of 4x (2x <-> 4x), where everything to suppress already
//    lies above the audible band. Latency: 3.5 samples at the 2x rate.
// The old 15-tap set suppressed only ~14 dB just past the transition, which let pick attacks at
// high gain fold back as grit.
namespace oversampling {
inline constexpr std::array<float, 47> Steep{
    -3.23680878e-05f, 0.0f, 0.000214604257f, 0.0f, -0.000690003601f, 0.0f, 0.00169065103f, 0.0f,
    -0.00353946785f, 0.0f, 0.00667084758f, 0.0f, -0.0116853841f, 0.0f, 0.0195116826f, 0.0f,
    -0.031906212f, 0.0f, 0.0532395992f, 0.0f, -0.0995345836f, 0.0f, 0.316062936f, 0.499995397f,
    0.316062936f, 0.0f, -0.0995345836f, 0.0f, 0.0532395992f, 0.0f, -0.031906212f, 0.0f,
    0.0195116826f, 0.0f, -0.0116853841f, 0.0f, 0.00667084758f, 0.0f, -0.00353946785f, 0.0f,
    0.00169065103f, 0.0f, -0.000690003601f, 0.0f, 0.000214604257f, 0.0f, -3.23680878e-05f};
inline constexpr std::array<float, 15> Light{
    -0.001682f, 0.0f, 0.0120f, 0.0f, -0.0540f, 0.0f, 0.293364f,
    0.500636f, 0.293364f, 0.0f, -0.0540f, 0.0f, 0.0120f, 0.0f, -0.001682f};
}

template <std::size_t Taps, const std::array<float, Taps>& Coefficients>
class HalfbandOversampler2x {
    static_assert(Taps % 4 == 3, "half-band: odd length with an odd centre index");
    static constexpr std::size_t Centre = (Taps - 1) / 2;

public:
    float process(float input, int channel, float (*nonlinear)(float, void*), void* state) noexcept
    {
        const auto ch = static_cast<std::size_t>(channel);
        const float up0 = m_up[ch].filter(input * 2.0f);
        const float up1 = m_up[ch].filter(0.0f);
        const float shaped0 = nonlinear(up0, state);
        const float shaped1 = nonlinear(up1, state);
        const float down0 = m_down[ch].filter(shaped0);
        (void)m_down[ch].filter(shaped1);
        return down0;
    }

    void reset() noexcept
    {
        for (auto& f : m_up) f = {};
        for (auto& f : m_down) f = {};
    }

private:
    struct Fir {
        std::array<float, Taps * 2> history{};   // each sample stored twice: windows stay contiguous
        std::size_t position = 0;

        float filter(float input) noexcept
        {
            position = position == 0 ? Taps - 1 : position - 1;
            history[position] = history[position + Taps] = input;
            const float* x = history.data() + position;   // x[k] = input k samples ago
            float output = Coefficients[Centre] * x[Centre];
            for (std::size_t k = 0; k < Taps; k += 2) output += Coefficients[k] * x[k];
            return output;
        }
    };

    std::array<Fir, 2> m_up{}, m_down{};
};

using Oversampler2x = HalfbandOversampler2x<47, oversampling::Steep>;
using Oversampler2xInner = HalfbandOversampler2x<15, oversampling::Light>;
