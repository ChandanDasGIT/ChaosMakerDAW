#pragma once

#include "plugins/mixfx/mixeffect.h"
#include "plugins/reverb/reverbcore.h"
#include "dsp/biquad.h"
#include "dsp/eqbanddesign.h"
#include "processor/smoother.h"

#include <array>

// Chaos Verb (restMap G7), the lush reverb. The input goes through a pre-delay, a band limit set
// by Colour and diffusing allpasses into an 8-line feedback delay network whose line lengths
// move slowly (Mod rate / depth: the chorus that makes the tail lush). Each trip round a line,
// lows, mids and highs lose what Decay times their multiplier asks for, so Bass and Treble decay
// shape the tail's colour over time. Early reflections tapped from the pre-delay are blended
// with the tail by Early / late. Mode sets the space; Colour the era.
class ChaosVerb final : public MixEffect {
public:
    ChaosVerb();

protected:
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    enum Index { Mode, Colour, Mix, PreDelay, Decay, Size, EarlyLate, Diffusion, Width,
                 ModRate, ModDepth, HighCut, LowCut, BassDecay, TrebleDecay };
    void updateShape() noexcept;
    void updateTone() noexcept;

    std::array<reverb::Delay, 8> m_lines;
    std::array<reverb::Allpass, 8> m_lineDiffusers;   // late diffusion, one per line
    std::array<float, 8> m_lineDiffuserLength{};
    std::array<float, 8> m_length{};
    std::array<float, 8> m_gainLow{}, m_gainMid{}, m_gainHigh{};
    std::array<reverb::OnePole, 8> m_bassSplit, m_trebleSplit, m_loopColour;
    std::array<float, 8> m_phase{};
    std::array<reverb::Allpass, 4> m_inputDiffusers;
    std::array<float, 4> m_inputDiffuserLength{};
    reverb::OnePole m_bandwidth;
    reverb::Delay m_preDelay;
    float m_earlySpread = 0.0f;
    float m_earlyLevel = 1.0f;
    float m_modDepthSamples = 0.0f;
    float m_lateDiffusion = 0.5f;
    std::array<float, 2> m_tone{};
    eqband::Design m_lowCut, m_highCut;
    std::array<std::array<biquad::State, eqband::MaxSections>, 2> m_lowState{}, m_highState{};
    std::array<float, 8> m_shapeKey{};
    Smoother m_mix, m_width, m_preDelaySmoother, m_earlyLate, m_depth;
};
