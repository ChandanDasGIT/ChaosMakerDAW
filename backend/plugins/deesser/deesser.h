#pragma once

#include "plugins/mixfx/mixeffect.h"
#include "dsp/biquad.h"
#include "dsp/eqbanddesign.h"
#include "processor/smoother.h"

#include <array>

// The de-esser (restMap G7). A high-pass at Frequency finds the sibilance; when that band goes
// over Threshold it is turned down, by at most Range. Split mode turns down only the highs, with
// a 24 dB/oct high shelf centred on Frequency whose cut follows the reduction (a dynamic EQ: at
// 0 dB it leaves the signal as it is, and the voice below the shelf is never touched); Wide mode
// dips the whole signal. Listen plays the detected band alone.
class DeEsser final : public MixEffect {
public:
    DeEsser();

protected:
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    enum Index { Frequency, Threshold, Listen, Range, Mode };
    eqband::Design m_highPass;
    float m_frequency = -1.0f;
    std::array<std::array<biquad::State, eqband::MaxSections>, 2> m_state{};
    eqband::Design m_shelf;
    float m_shelfDb = 0.0f;       // the cut the shelf was last designed for
    std::array<std::array<biquad::State, eqband::MaxSections>, 2> m_shelfState{};
    int m_shelfCountdown = 0;
    void designShelf(float cutDb) noexcept;
    std::array<Smoother, 2> m_smoothers;   // threshold, range
    float m_reduction = 0.0f;              // dB
    float m_attack = 0.0f;
    float m_release = 0.0f;
};
