#include "instruments/synth/synthoscillator.h"

#include <cmath>
#include <numbers>

namespace synth {

double polyBlep(double t, double dt) noexcept
{
    if (dt <= 0.0) return 0.0;
    if (t < dt) {                                   // just after the jump
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {                             // just before it
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

float WaveOscillator::next(Wave wave, double dt, double pulseWidth) noexcept
{
    const double t = m_phase;
    double out = 0.0;
    switch (wave) {
    case Wave::Saw:
        out = 2.0 * t - 1.0 - polyBlep(t, dt);
        break;
    case Wave::Pulse: {
        const double w = pulseWidth;
        out = t < w ? 1.0 : -1.0;
        out += polyBlep(t, dt);                     // the rise at 0
        double fall = t - w;
        if (fall < 0.0) fall += 1.0;
        out -= polyBlep(fall, dt);                  // the fall at w
        out -= 2.0 * w - 1.0;                       // no DC at narrow widths
        break;
    }
    case Wave::Triangle:
        // Rising through 0 at the start like sine: +1 at 1/4, -1 at 3/4.
        out = t < 0.25 ? 4.0 * t : t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0;
        break;
    case Wave::Sine:
        out = std::sin(2.0 * std::numbers::pi * t);
        break;
    }
    m_phase += dt;
    if (m_phase >= 1.0) m_phase -= std::floor(m_phase);
    return float(out);
}

} // namespace synth
