#pragma once

#include "plugins/mixfx/mixeffect.h"
#include "plugins/reverb/reverbcore.h"
#include "dsp/biquad.h"
#include "dsp/eqbanddesign.h"
#include "processor/smoother.h"

#include <array>

// The plain reverb (restMap G7): Room, Plate or Hall. A feedback delay network of 8 lines mixed
// by a Hadamard matrix, each line losing what Decay asks for per trip and its highs to Damping;
// the input is smeared by allpasses first (more for the plate), and the room and hall add early
// reflections. Low / high cut shape the reverb only; Width and Mix (equal power) set how it
// joins the dry track.
class RoomReverb final : public MixEffect {
public:
    RoomReverb();

protected:
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    enum Index { Type, Size, Decay, PreDelay, Damping, LowCut, HighCut, Width, Mix };
    void updateShape() noexcept;
    void updateTone() noexcept;

    std::array<reverb::Delay, 8> m_lines;
    std::array<reverb::OnePole, 8> m_damping;
    std::array<float, 8> m_length{};      // samples
    std::array<float, 8> m_gain{};
    std::array<reverb::Allpass, 6> m_diffusers;
    std::array<float, 6> m_diffuserLength{};
    int m_diffuserCount = 4;
    float m_diffusion = 0.6f;
    reverb::Delay m_preDelay;             // pre-delay and the early reflections' taps
    float m_earlyGain = 0.0f;
    float m_earlySpread = 0.0f;           // samples
    std::array<float, 2> m_tone{};        // low and high cut the filters were designed for
    eqband::Design m_lowCut, m_highCut;
    std::array<std::array<biquad::State, eqband::MaxSections>, 2> m_lowState{}, m_highState{};
    std::array<float, 4> m_shapeKey{-1, -1, -1, -1};   // type, size, decay, damping last applied
    Smoother m_mix, m_width, m_preDelaySmoother;
};
