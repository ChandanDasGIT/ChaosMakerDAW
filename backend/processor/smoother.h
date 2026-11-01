#pragma once
#include <cmath>

// Glides toward a target, so a moving knob does not click.
class Smoother {
public:
    void prepare(double sampleRate, float milliseconds) noexcept {
        m_coefficient = 1.0f - std::exp(-1.0f / float(sampleRate * milliseconds * 0.001));
    }
    void reset(float value) noexcept { m_value = value; }
    float next(float target) noexcept {
        m_value += m_coefficient * (target - m_value);
        return m_value;
    }

private:
    float m_coefficient = 1.0f;
    float m_value = 0.0f;
};
