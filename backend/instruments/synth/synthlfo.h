#pragma once
#include "instruments/synth/synthsettings.h"

#include <cstdint>

// The synth's LFO: one phase for the whole instrument, so every held note wobbles together.
// Values run -1..1. Random holds a new value for each cycle (sample and hold).
namespace synth {

class LowFrequencyOscillator {
public:
    void reset() noexcept;
    // Moves on by `cycles` (rate * seconds). A new random value at each wrap.
    void advance(double cycles) noexcept;
    // Puts the phase at `phase` (any real number; only its fraction counts). Used to lock a
    // tempo-synced LFO to the song: a wrap draws a new random value too.
    void setPhase(double phase) noexcept;
    double phase() const noexcept { return m_phase; }
    float value(LfoShape shape) const noexcept;

private:
    void draw() noexcept;

    double m_phase = 0.0;
    float m_held = 0.0f;
    std::uint32_t m_seed = 0x2545F491u;
};

// The rate in Hz: `lfo.rateHz`, or the note division at `bpm` when synced.
double lfoRateHz(const Lfo& lfo, double bpm) noexcept;

} // namespace synth
