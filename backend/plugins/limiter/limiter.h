#pragma once

#include "plugins/mixfx/mixeffect.h"
#include "plugins/reverb/reverbcore.h"

#include <array>
#include <vector>

// The limiter (restMap G7 extras): Gain pushes the track up, nothing goes over Ceiling. A 1 ms
// look-ahead sees each peak coming (the track is 1 ms late, like any look-ahead limiter): the
// gain each sample needs is held at its minimum over the look-ahead (L + 1 samples), then
// averaged over L, so it has glided down to a peak's level by the time the peak leaves the
// delay, without a click; Release lets it back up. A final clip at the ceiling is a safety net.
class Limiter final : public MixEffect {
public:
    Limiter();
    int latencySamples() const noexcept override { return m_lookahead; }

protected:
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    enum Index { Gain, Ceiling, Release };
    std::array<reverb::Delay, 2> m_delay;
    std::vector<float> m_window;   // the gain each of the last L + 1 samples needs
    int m_windowPos = 0;
    std::vector<float> m_average;  // the held minimum over the last L samples, and their sum
    int m_averagePos = 0;
    double m_averageSum = 0.0;
    int m_lookahead = 48;
    float m_gain = 1.0f;           // the gain now
};
