#pragma once
#include "instruments/synth/synthsettings.h"

// An ADSR envelope, 0..1, one value per sample.
//
//   attack    a straight rise from wherever the level is (no click on a retriggered voice)
//   decay     falls towards sustain, 1 % of the way left after `decay` seconds
//   sustain   held while the key is
//   release   falls towards 0 the same way; below -80 dB the envelope is idle
//
// With sustain 0 the envelope goes idle at the end of the decay even with the key held, so a
// plucked sound frees its voice. Times may change while it runs; they apply at once.
namespace synth {

class AdsrEnvelope {
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void configure(const Envelope& times, double sampleRate) noexcept;
    void gateOn() noexcept;                         // attack, from the present level
    void gateOff() noexcept;                        // release, unless idle
    void fadeOut(double seconds, double sampleRate) noexcept;   // a quick release (a stolen voice)
    void reset() noexcept;                          // idle and silent at once

    float next() noexcept;
    float level() const noexcept { return float(m_level); }
    Stage stage() const noexcept { return m_stage; }
    bool idle() const noexcept { return m_stage == Stage::Idle; }
    bool releasing() const noexcept { return m_stage == Stage::Release; }

private:
    static constexpr double Silent = 1e-4;          // -80 dB

    Stage m_stage = Stage::Idle;
    double m_level = 0.0;
    double m_attackStep = 1.0;
    double m_decayCoef = 0.0, m_releaseCoef = 0.0;
    double m_sustain = 1.0;
    bool m_fading = false;                          // fadeOut() runs: configure() leaves it
};

// The per-sample multiplier that leaves 1 % after `seconds`.
double fallCoefficient(double seconds, double sampleRate) noexcept;

} // namespace synth
