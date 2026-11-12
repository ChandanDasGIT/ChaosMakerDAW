#pragma once
#include "instruments/synth/synthsettings.h"

// The synth's filter: a state-variable filter in the topology-preserving (trapezoidal) form, so
// its cutoff can sweep every few samples without clicks or blowing up.
//
//   LowPass12   one stage, 12 dB / octave above the cutoff
//   LowPass24   two stages, 24 dB / octave; the resonance is on the first
//   HighPass    one stage, 12 dB / octave below the cutoff
//   BandPass    one stage, 0 dB at the cutoff, falling either side
//
// Resonance 0 is a gentle, unpeaked corner; towards 1 the cutoff rings.
namespace synth {

class SvfFilter {
public:
    // Any thread that owns the filter; cheap enough to call every few samples.
    void set(FilterMode mode, double cutoffHz, double resonance, double sampleRate) noexcept;
    void reset() noexcept;
    float process(float x) noexcept;

private:
    struct Stage {
        double a1 = 0.0, a2 = 0.0, a3 = 0.0, k = 2.0;
        double ic1 = 0.0, ic2 = 0.0;
        // Returns low (v2) and band (v1) through the references.
        void tick(double v0, double& low, double& band) noexcept;
        void design(double g, double damping) noexcept;
    };

    FilterMode m_mode = FilterMode::LowPass24;
    Stage m_first, m_second;
};

} // namespace synth
