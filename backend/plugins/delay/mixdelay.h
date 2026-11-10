#pragma once

#include "plugins/mixfx/mixeffect.h"
#include "plugins/reverb/reverbcore.h"
#include "processor/smoother.h"

#include <array>

// The mixing delay (restMap G7 extras): echoes synced to the song's tempo or set in ms, with
// feedback filtered by low and high cut each repeat, and ping-pong between the sides. Named
// MixDelay: the guitar suite has its own delay pedal.
class MixDelay final : public MixEffect {
public:
    MixDelay();
    // The delay in ms for a note division at a tempo (the "Note" choices, in order).
    static double divisionMs(int division, double bpm) noexcept;

protected:
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    enum Index { Sync, Division, Time, PingPong, Feedback, LowCut, HighCut, Mix };
    std::array<reverb::Delay, 2> m_lines;
    std::array<reverb::OnePole, 2> m_low, m_high;
    std::array<float, 2> m_cuts{-1, -1};
    Smoother m_time, m_feedback, m_mix;
    bool m_timeStarted = false;   // the first block starts at the time instead of gliding to it
};
