#pragma once

#include "analysis/beattracker.h"
#include "analysis/onsets.h"

// The time signature and where bars start (restMap G20, reused by G22), from the beats and the
// drum ranges' rises. A drum part repeats bar by bar: the kick and snare on each beat are
// compared with those L beats later, and the L that repeats best is the bar (3 beats for 3/4,
// else 4). Between the beats: hits on the thirds of a beat mean a compound meter (6/8, 9/8,
// 12/8, the beat being a dotted quarter). A fast beat (150 BPM and up) may be eighth notes:
// then 6 or 7 of them can make the bar (6/8, 7/8). The bar starts on the beat where the kick
// is strongest and the snare weakest. 2/4 reads as 4/4; odd meters are a best guess.
namespace analysis {

struct Meter {
    int numerator = 4;
    int denominator = 4;
    int beatsPerBar = 4;                   // counted beats (the tracker's) per bar
    double beatQuarters = 1.0;             // one tracker beat in quarter notes (0.5, 1, 1.5)
    bool triple = false;                   // the beat splits in three
    int downbeat = 0;                      // the index (0..beatsPerBar-1) of a beat that starts a bar

    double barQuarters() const noexcept { return numerator * 4.0 / denominator; }
};

Meter findMeter(const Envelopes& envelopes, const Beat& beat);

} // namespace analysis
