#pragma once

#include "plugins/mixfx/mixeffect.h"

// The gate (restMap G7 extras): silences what is under Threshold, by Range. It opens fast
// (Attack), stays open through short gaps (Hold), then closes (Release). It closes only 4 dB
// under the threshold (hysteresis), so a level hovering at the threshold does not chatter.
class Gate final : public MixEffect {
public:
    Gate();

protected:
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    enum Index { Threshold, Range, Attack, Hold, Release };
    float m_envelope = 0.0f;   // the detector's level
    float m_reduction = 0.0f;  // dB now
    int m_holdLeft = 0;
    bool m_open = false;
    float m_envelopeRelease = 0.0f;
};
