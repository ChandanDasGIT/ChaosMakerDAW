#include "instruments/synth/synthfilter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace synth {

void SvfFilter::Stage::design(double g, double damping) noexcept
{
    k = damping;
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

void SvfFilter::Stage::tick(double v0, double& low, double& band) noexcept
{
    const double v3 = v0 - ic2;
    const double v1 = a1 * ic1 + a2 * v3;
    const double v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    band = v1;
    low = v2;
}

void SvfFilter::set(FilterMode mode, double cutoffHz, double resonance, double sampleRate) noexcept
{
    m_mode = mode;
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double hz = std::clamp(cutoffHz, MinCutoffHz, 0.45 * rate);
    const double g = std::tan(std::numbers::pi * hz / rate);
    // damping k = 1/Q: sqrt 2 (no peak) at resonance 0 down to 0.05 (Q 20) at 1.
    const double r = std::clamp(resonance, 0.0, 1.0);
    const double k = std::numbers::sqrt2 - (std::numbers::sqrt2 - 0.05) * r;
    m_first.design(g, k);
    m_second.design(g, std::numbers::sqrt2);
}

void SvfFilter::reset() noexcept
{
    m_first.ic1 = m_first.ic2 = 0.0;
    m_second.ic1 = m_second.ic2 = 0.0;
}

float SvfFilter::process(float input) noexcept
{
    const double x = input;
    double low = 0.0, band = 0.0;
    m_first.tick(x, low, band);
    switch (m_mode) {
    case FilterMode::LowPass12:
        return float(low);
    case FilterMode::LowPass24: {
        double low2 = 0.0, band2 = 0.0;
        m_second.tick(low, low2, band2);
        return float(low2);
    }
    case FilterMode::HighPass:
        return float(x - m_first.k * band - low);
    case FilterMode::BandPass:
        return float(m_first.k * band);
    }
    return float(low);
}

} // namespace synth
