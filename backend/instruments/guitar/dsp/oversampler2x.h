#pragma once

#include <array>
#include <cstddef>

// Small polyphase-style 2x half-band up/downsampler. State is fixed-size and
// all work is stack/register-only in process().
class Oversampler2x {
public:
    float process(float input, int channel, float (*nonlinear)(float, void*), void* state) noexcept
    {
        const auto ch = static_cast<std::size_t>(channel);
        const float up0 = filter(m_upHistory[ch], input * 2.0f);
        const float up1 = filter(m_upHistory[ch], 0.0f);
        const float shaped0 = nonlinear(up0, state);
        const float shaped1 = nonlinear(up1, state);
        const float down0 = filter(m_downHistory[ch], shaped0);
        (void)filter(m_downHistory[ch], shaped1);
        return down0;
    }

    void reset() noexcept
    {
        for (auto& history : m_upHistory) history.fill(0.0f);
        for (auto& history : m_downHistory) history.fill(0.0f);
    }

private:
    static constexpr std::array<float, 15> Coefficients{
        -0.001682f, 0.0f, 0.0120f, 0.0f, -0.0540f, 0.0f, 0.293364f,
        0.500636f, 0.293364f, 0.0f, -0.0540f, 0.0f, 0.0120f, 0.0f, -0.001682f
    };

    static float filter(std::array<float, 15>& history, float input) noexcept
    {
        history[0] = input;
        float output = 0.0f;
        for (std::size_t tap = 0; tap < Coefficients.size(); ++tap)
            output += Coefficients[tap] * history[tap];
        for (std::size_t tap = history.size() - 1; tap > 0; --tap)
            history[tap] = history[tap - 1];
        return output;
    }

    std::array<std::array<float, 15>, 2> m_upHistory{};
    std::array<std::array<float, 15>, 2> m_downHistory{};
};
