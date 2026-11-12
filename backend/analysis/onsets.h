#pragma once

#include "analysis/hpss.h"
#include "analysis/spectrogram.h"

#include <vector>

// Where hits start (restMap G20): the rise of the drum part's level per frame ("spectral flux"
// on a log scale, so quiet and loud songs read alike), summed over all bands and over three
// drum ranges: low (kick), mid (snare body and crack) and high (hi-hats).
namespace analysis {

struct Envelopes {
    double frameRate = 100.0;
    std::vector<float> total, low, mid, high;   // the rise per frame, >= 0
    std::vector<float> highLevel;               // the whole sound's high-range level (how long a hat rings)
    std::vector<float> lowPlain;                // the low range's rise in plain level, not log: how hard the kick is hit
    // Each drum's own rise (drumnmf), over 3 frames, when known: the meter reads these first.
    std::vector<float> kick, snare, hat;
    // How much the harmonic part (chords, bass) changes per frame, when known: chords mostly
    // change on bar lines, so the meter's bar start leans on it in songs.
    std::vector<float> change;
    float harmonicShare = 0.0f;               // the harmonic part's share of the whole sound (0..1)
    // Hardly anything but drums (a loop or stem, not a song): the kit's own sounds were learnt
    // and the drums' rises come from compressed levels.
    bool drumsOnly = false;
};

// The change envelope from the harmonic part (the separation's): the mean log change of each
// band over 80 ms, rises only.
std::vector<float> harmonicChange(const BandSpectrogram& spectrogram, const std::vector<float>& harmonic);

// The drum ranges, in Hz.
constexpr double LowFromHz = 30.0, LowToHz = 120.0;
constexpr double MidFromHz = 150.0, MidToHz = 2500.0;
constexpr double HighFromHz = 6000.0;

// From the spectrogram and its percussive part (the separation's).
Envelopes envelopes(const BandSpectrogram& spectrogram, const std::vector<float>& percussive);

struct Onset {
    int frame = 0;
    double seconds = 0.0;
    float total = 0.0f, low = 0.0f, mid = 0.0f, high = 0.0f;   // the rises around the hit
};

// The peaks of the total rise that stand out from their surroundings (an adaptive threshold),
// at least 30 ms apart, in time order.
std::vector<Onset> pickOnsets(const Envelopes& envelopes);

// A value of `values` at a fractional frame (linear), 0 outside.
float sampleAt(const std::vector<float>& values, double frame) noexcept;
// The largest of `values` within `half` frames of `frame`.
float peakNear(const std::vector<float>& values, double frame, int half) noexcept;
// The value below which `fraction` of `values` lie (0 when empty).
float percentile(std::vector<float> values, double fraction);

} // namespace analysis
