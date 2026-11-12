#pragma once
#include "instruments/synth/synthsettings.h"

// One oscillator: a phase from 0 to 1 and the wave read from it.
//
// Saw and pulse jump once or twice a cycle. A plain jump has harmonics far above Nyquist that
// fold back down as inharmonic whistles on high notes; PolyBLEP rounds each jump over the two
// samples around it, which takes most of that out. Triangle has no jumps (its harmonics fall
// fast) and sine has no harmonics, so both are read directly.
namespace synth {

class WaveOscillator {
public:
    void reset(double phase = 0.0) noexcept { m_phase = phase; }
    double phase() const noexcept { return m_phase; }

    // One sample, then the phase moves on by `increment` (frequency / sample rate, below 0.5).
    // `pulseWidth` is for Wave::Pulse only.
    float next(Wave wave, double increment, double pulseWidth) noexcept;

private:
    double m_phase = 0.0;
};

// The correction PolyBLEP adds near a jump of size 2 at phase 0 (t: phase, dt: increment).
double polyBlep(double t, double dt) noexcept;

} // namespace synth
