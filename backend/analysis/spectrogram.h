#pragma once

#include "analysis/progress.h"

#include <vector>

// The sound's spectrum over time in log-spaced bands (restMap G20): 10 ms frames, 8 bands per
// octave from 30 Hz up to 16 kHz (or 45 % of the sample rate). Enough detail to tell a kick from
// a snare or a hi-hat, and small: a five-minute song is about two megabytes.
namespace analysis {

struct BandSpectrogram {
    static constexpr double LowestHz = 30.0;
    static constexpr double HighestHz = 16000.0;
    static constexpr int BandsPerOctave = 8;

    double sampleRate = 48000.0;
    int hop = 480;                         // samples per frame
    int frames = 0;
    int bands = 0;
    std::vector<float> centreHz;           // per band
    std::vector<float> magnitude;          // frames * bands, frame-major: the RMS level per FFT bin

    double frameRate() const noexcept { return sampleRate / hop; }
    float at(int frame, int band) const noexcept { return magnitude[std::size_t(frame) * bands + band]; }
    // The first band whose centre is at or above hz (bands when none).
    int bandAt(double hz) const noexcept;
};

// Frame f covers the window centred on sample f * hop. Empty (frames 0) when the sound is empty
// or the analysis is cancelled.
BandSpectrogram bandSpectrogram(const std::vector<float>& mono, double sampleRate, const Progress& progress = {});

} // namespace analysis
