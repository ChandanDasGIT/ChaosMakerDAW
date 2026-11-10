#pragma once

#include "plugins/mixfx/mixeffect.h"
#include "plugins/reverb/reverbcore.h"
#include "instruments/guitar/dsp/oversampler2x.h"
#include "processor/smoother.h"
#include "dsp/biquad.h"
#include "dsp/eqbanddesign.h"

#include <array>

// Saturation (restMap G7 extras): Tape (smooth, round), Tube (warm, asymmetric: even
// harmonics) or Clip (edgy). Run at twice the sample rate so the harmonics it adds do not fold
// back as grit. The level stays about where it was as Drive rises (the curve's gain at -12 dBFS is
// taken back off); Tone tilts the result darker or brighter around 1 kHz (a tilt shelf, up to
// 6 dB each way at the ends). Mix blends with the dry track, delayed by the oversampler's latency
// so the two line up.
class Saturation final : public MixEffect {
public:
    Saturation();
    static constexpr int Latency = 23;   // the 2x oversampler's, in samples
    int latencySamples() const noexcept override { return Latency; }
    // The curves themselves (any drive already applied): smooth, odd-and-even, or edgy; all
    // bounded to about +-1.5.
    static float shape(int type, float x) noexcept;

protected:
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    enum Index { Type, Drive, Tone, Output, Mix };
    struct ShapeState { int type; float drive; };
    Oversampler2x m_oversampler;
    std::array<reverb::Delay, 2> m_dry;
    eqband::Design m_tilt;
    float m_tiltTone = -1.0f;   // the tone the tilt was designed for
    int m_tiltCountdown = 0;
    std::array<std::array<biquad::State, eqband::MaxSections>, 2> m_tiltState{};
    void designTilt(float tone) noexcept;
    Smoother m_drive, m_tone, m_output, m_mix;
};
