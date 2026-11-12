#include "instruments/synth/synthlfo.h"
#include "instruments/guitar/dsp/tempodivision.h"

#include <cmath>
#include <numbers>

namespace synth {

void LowFrequencyOscillator::reset() noexcept
{
    m_phase = 0.0;
    m_seed = 0x2545F491u;
    draw();
}

void LowFrequencyOscillator::draw() noexcept
{
    m_seed ^= m_seed << 13; m_seed ^= m_seed >> 17; m_seed ^= m_seed << 5;
    m_held = float(m_seed) * (2.0f / 4294967296.0f) - 1.0f;
}

void LowFrequencyOscillator::advance(double cycles) noexcept
{
    if (!(cycles > 0.0)) return;
    m_phase += cycles;
    if (m_phase >= 1.0) {
        m_phase -= std::floor(m_phase);
        draw();
    }
}

void LowFrequencyOscillator::setPhase(double phase) noexcept
{
    if (!std::isfinite(phase)) return;
    const double next = phase - std::floor(phase);
    if (next < m_phase) draw();                     // wrapped (or jumped back)
    m_phase = next;
}

float LowFrequencyOscillator::value(LfoShape shape) const noexcept
{
    const double t = m_phase;
    switch (shape) {
    case LfoShape::Sine: return float(std::sin(2.0 * std::numbers::pi * t));
    case LfoShape::Triangle: return float(t < 0.25 ? 4.0 * t : t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0);
    case LfoShape::Saw: return float(2.0 * t - 1.0);
    case LfoShape::Square: return t < 0.5 ? 1.0f : -1.0f;
    case LfoShape::Random: return m_held;
    }
    return 0.0f;
}

double lfoRateHz(const Lfo& lfo, double bpm) noexcept
{
    return lfo.sync ? tempo::noteFrequency(bpm, lfo.division) : lfo.rateHz;
}

} // namespace synth
