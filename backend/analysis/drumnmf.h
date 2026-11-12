#pragma once

#include "analysis/progress.h"
#include "analysis/spectrogram.h"

#include <array>
#include <vector>

// Which drum plays when (restMap G20): the drum part of the spectrogram explained as a sum of
// spectral shapes times their levels over time (non-negative matrix factorisation, the
// Kullback-Leibler kind). The kick, snare and hi-hat shapes are this recording's own where
// they could be learnt (kitshapes: drums only), held firmly; else generic ones that adapt a
// little (semi-adaptive). A few free shapes take whatever else is left (bass, pad, cymbals,
// toms; they start as the kit's other sounds), so it is not forced onto the drums. Hits played together
// (kick and hat, snare and hat) come apart, as each drum has its own level. Hand-written
// multiplicative updates, no learning from other recordings.
namespace analysis {

enum Drum { KickDrum = 0, SnareDrum = 1, HatDrum = 2, DrumCount = 3 };

struct DrumActivations {
    double frameRate = 100.0;
    std::array<std::vector<float>, DrumCount> level;   // each drum's level per frame
    std::array<std::vector<float>, DrumCount> rise;    // its rise per frame (>= 0): where it is hit
};

constexpr int FreeShapes = 4;
// The free shapes take only what lies below this (bass, pad, the low of cymbals and toms), so a
// hi-hat played with a kick is not taken for a new "kick and hat" sound.
constexpr double FreeShapesBelowHz = 3000.0;
constexpr int NmfIterations = 40;
constexpr float Compression = 0.5f;        // levels are raised to this before the factorisation
constexpr float OwnKeep = 0.9f;            // how firmly a shape learnt from this recording is held

// The generic starting shape of a drum over the spectrogram's bands (sums to 1).
std::vector<float> drumTemplate(Drum drum, const BandSpectrogram& spectrogram, bool drumsOnly);

// From the spectrogram's layout of cells (frames * bands): its percussive part. Empty levels
// when cancelled.
// drumsOnly (a drum loop or stem): this recording's own drum sounds are learnt first
// (kitshapes) and used in place of the generic shapes, on compressed levels. In a whole song
// the other instruments' notes would be learnt as drums, so there the generic shapes stay, on
// plain levels.
DrumActivations drumActivations(const BandSpectrogram& spectrogram, const std::vector<float>& cells, bool drumsOnly,
                                 const Progress& progress = {});

} // namespace analysis
