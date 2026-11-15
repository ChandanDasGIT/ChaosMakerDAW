#pragma once

#include <optional>
#include <vector>

// Tap tempo (restMap G17): the BPM from taps in time with the music. No Qt or audio-thread work;
// the caller passes each tap's time in seconds from any steady clock.
class TapTempo {
public:
    static constexpr double ResetAfterSeconds = 2.0;   // a longer pause starts a new count
    static constexpr int MaxIntervals = 8;             // the average follows the latest taps
    static constexpr double ChangeRatio = 0.25;        // a tap this far off the average: a new tempo
                                                       // (hand taps wobble ~10-15 %)
    static constexpr double MinBpm = 20.0;
    static constexpr double MaxBpm = 300.0;

    // A tap. The tempo from the taps so far (the average interval, rounded to 0.1 BPM, within
    // MinBpm..MaxBpm); nullopt for the first tap of a count or a time that goes backwards.
    std::optional<double> tap(double seconds);
    void reset() { m_taps.clear(); }
    int taps() const { return static_cast<int>(m_taps.size()); }

private:
    std::vector<double> m_taps;   // the current count, oldest first (at most MaxIntervals + 1)
};
