#include "instruments/bass/bassexciter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace bass {

namespace {

double lerp(double a, double b, double t) noexcept { return a + (b - a) * t; }

// A one-pole coefficient for a corner at `hz`.
double onePole(double hz, double sampleRate) noexcept
{
    return 1.0 - std::exp(-2.0 * std::numbers::pi * hz / sampleRate);
}

} // namespace

void Exciter::start(NoteKind kind, double strength, double brightness, double strikeAt, double period,
                    double sampleRate) noexcept
{
    strength = std::clamp(strength, 0.0, 1.0);
    brightness = std::clamp(brightness, 0.0, 1.0);
    double widthMs = 0.0, amplitude = 0.0, clickMs = 0.0, click = 0.0, clickLowHz = 0.0, clickHighHz = 0.0;
    switch (kind) {
    case NoteKind::Finger:
        widthMs = lerp(4.5, 1.6, brightness);
        amplitude = 1.0;
        break;
    case NoteKind::Slap:
        widthMs = lerp(0.9, 0.35, brightness);
        amplitude = 0.75;
        click = 0.14; clickMs = 9.0; clickLowHz = 900.0; clickHighHz = 4500.0;
        strikeAt = std::max(strikeAt, 0.28);       // the thumb lands at the end of the neck
        break;
    case NoteKind::Pop:
        widthMs = lerp(0.8, 0.3, brightness);
        amplitude = 0.8;
        click = 0.18; clickMs = 6.0; clickLowHz = 1500.0; clickHighHz = 7000.0;
        strikeAt = std::max(strikeAt, 0.25);
        break;
    case NoteKind::Muted:
        widthMs = lerp(7.0, 4.0, brightness);
        amplitude = 0.9;
        break;
    }
    m_width = std::max(2.0, widthMs * 0.001 * sampleRate);
    m_amplitude = amplitude * strength;
    m_combDelay = std::clamp(strikeAt, 0.02, 0.5) * std::max(period, 4.0);
    m_n = 0.0;
    m_left = int(std::ceil(m_width + m_combDelay)) + 1;

    m_clickLevel = click * strength;
    m_clickDecay = clickMs > 0.0 ? std::exp(-1.0 / (clickMs * 0.001 * sampleRate / 4.6)) : 0.0;
    m_clickHp = clickLowHz > 0.0 ? onePole(clickLowHz, sampleRate) : 0.0;
    m_clickLp = clickHighHz > 0.0 ? onePole(std::min(clickHighHz, 0.45 * sampleRate), sampleRate) : 0.0;
    m_hpState = m_lpState = 0.0;
    if (m_clickLevel > 0.0) m_left = std::max(m_left, int(clickMs * 0.001 * sampleRate * 1.5));
}

double Exciter::bump(double n) const noexcept
{
    if (n < 0.0 || n > m_width) return 0.0;
    return 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / m_width);   // a Hann bump
}

float Exciter::noise() noexcept
{
    m_seed = m_seed * 1664525u + 1013904223u;
    return float(int32_t(m_seed)) * (1.0f / 2147483648.0f);
}

void Exciter::next(float& toString, float& direct) noexcept
{
    toString = 0.0f;
    direct = 0.0f;
    if (m_left <= 0) return;
    --m_left;
    toString = float(m_amplitude * (bump(m_n) - bump(m_n - m_combDelay)));
    m_n += 1.0;

    if (m_clickLevel > 1e-6) {
        // Noise band-passed between the click's corners, dying away.
        const double x = noise();
        m_hpState += m_clickHp * (x - m_hpState);
        const double high = x - m_hpState;
        m_lpState += m_clickLp * (high - m_lpState);
        direct = float(m_clickLevel * m_lpState * 2.0);
        m_clickLevel *= m_clickDecay;
    }
}

} // namespace bass
