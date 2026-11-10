#pragma once

#include "plugins/mixfx/mixeffect.h"
#include "dsp/biquad.h"
#include "dsp/eqbanddesign.h"
#include "processor/smoother.h"

#include <array>

// The mixing compressor (restMap G7), stereo-linked, for any track. Styles: Clean (peak
// detector), Smooth (RMS detector, release slows while it is held down, like an optical unit),
// Punchy (feedback: the detector hears the compressor's own output). Its detector ignores the
// lows below "Detector low cut" so bass does not make it pump.
class MixCompressor final : public MixEffect {
public:
    MixCompressor();

protected:
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    enum Index { Style, Threshold, Ratio, Knee, Attack, Release, LowCut, Makeup, AutoMakeup, Mix };
    std::array<Smoother, 5> m_smoothers;   // threshold, ratio, knee, makeup, mix
    eqband::Design m_lowCut;
    float m_lowCutHz = -1.0f;
    std::array<std::array<biquad::State, eqband::MaxSections>, 2> m_lowCutState{};
    float m_reduction = 0.0f;     // dB
    float m_held = 0.0f;          // dB, a slow average of the reduction (Smooth's release)
    float m_meanSquare = 0.0f;    // Smooth's RMS detector
    std::array<float, 2> m_lastOut{};
    float m_rmsCoefficient = 0.0f;
    float m_heldCoefficient = 0.0f;
};
