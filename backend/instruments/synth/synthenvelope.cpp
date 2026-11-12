#include "instruments/synth/synthenvelope.h"

#include <algorithm>
#include <cmath>

namespace synth {

double fallCoefficient(double seconds, double sampleRate) noexcept
{
    const double frames = std::max(seconds * sampleRate, 1.0);
    return std::exp(std::log(0.01) / frames);
}

void AdsrEnvelope::configure(const Envelope& e, double rate) noexcept
{
    m_attackStep = 1.0 / std::max(e.attack * rate, 1.0);
    m_decayCoef = fallCoefficient(e.decay, rate);
    m_sustain = std::clamp(e.sustain, 0.0, 1.0);
    if (!m_fading) m_releaseCoef = fallCoefficient(e.release, rate);   // a fade-out keeps its own
}

void AdsrEnvelope::gateOn() noexcept
{
    m_stage = Stage::Attack;
    m_fading = false;
}

void AdsrEnvelope::gateOff() noexcept
{
    if (m_stage != Stage::Idle) m_stage = Stage::Release;
}

void AdsrEnvelope::fadeOut(double seconds, double rate) noexcept
{
    if (m_stage == Stage::Idle) return;
    m_stage = Stage::Release;
    m_fading = true;
    m_releaseCoef = fallCoefficient(seconds, rate);
}

void AdsrEnvelope::reset() noexcept
{
    m_stage = Stage::Idle;
    m_level = 0.0;
    m_fading = false;
}

float AdsrEnvelope::next() noexcept
{
    switch (m_stage) {
    case Stage::Idle:
        m_level = 0.0;
        break;
    case Stage::Attack:
        m_level += m_attackStep;
        if (m_level >= 1.0) { m_level = 1.0; m_stage = Stage::Decay; }
        break;
    case Stage::Decay:
        m_level = m_sustain + (m_level - m_sustain) * m_decayCoef;
        if (m_level - m_sustain < Silent) {
            m_level = m_sustain;
            m_stage = m_sustain < Silent ? Stage::Idle : Stage::Sustain;
        }
        break;
    case Stage::Sustain:
        m_level = m_sustain;
        break;
    case Stage::Release:
        m_level *= m_releaseCoef;
        if (m_level < Silent) { m_level = 0.0; m_stage = Stage::Idle; }
        break;
    }
    return float(m_level);
}

} // namespace synth
