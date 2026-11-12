#pragma once

#include "analysis/progress.h"
#include "analysis/spectrogram.h"

#include <vector>

// Harmonic / percussive separation (restMap G20), so drums can be found in a whole song.
// Sustained sounds (chords, bass, voice) are smooth along time and narrow across frequency;
// hits are short in time and spread over many bands. A median along time keeps the first, a
// median across bands the second; each cell goes to the part that holds more of it (soft
// masks). Hand-written, no learning.
namespace analysis {

struct Separation {
    std::vector<float> percussive;         // the spectrogram's layout: frames * bands
    std::vector<float> harmonic;
};

constexpr int HarmonicHalfFrames = 8;      // the median along time spans 17 frames (170 ms)
constexpr int PercussiveHalfBands = 4;     // the median across frequency spans 9 bands (1.1 octaves)

// Empty parts when the spectrogram is empty or the analysis is cancelled.
Separation separate(const BandSpectrogram& spectrogram, const Progress& progress = {});

} // namespace analysis
