#pragma once

#include "analysis/progress.h"

#include <array>
#include <vector>

// Which of the twelve note names sound, over time (restMap G21): the chord analyzer's ears.
//
//   1. The spectrum every 50 ms from a long window (about 0.18 s: semitones come apart down to
//      the bass), from 55 Hz to 2.1 kHz.
//   2. What stays (a median along time, per frequency) is kept: held notes; drum hits and
//      consonants fall away.
//   3. The peaks of each frame's spectrum, their frequency read between the bins. Each peak
//      goes to the note name it is nearest to, more the nearer it is, on a log level so one
//      loud note does not hide the others.
//      A peak that is the 3rd or 5th overtone of a louder peak below counts less: it would
//      otherwise read as a fifth or a major third that nobody plays.
//   4. The tuning first: how far the peaks lie from the notes of A = 440 Hz on average; the
//      notes are then counted from the song's own pitch (a recording a little sharp or flat).
//
// Two rows per frame: `notes` (130 Hz to 2.1 kHz: the chord) and `bass` (55 to 260 Hz: the
// note under it, which tells an A minor from a C with the same notes on top). An instrument's
// overtones count too (a C also sounds a little G and E): the chord finder allows for that.
// Hand-written, no learning.
namespace analysis {

using Chroma = std::array<float, 12>;      // C, C#, D, ... B

struct Chromagram {
    double frameRate = 20.0;               // frames per second
    int frames = 0;
    double tuningCents = 0.0;              // the song's pitch against A = 440 Hz, -50 .. +50
    std::vector<Chroma> notes, bass;       // per frame, levels >= 0 (not scaled)

    double seconds() const noexcept { return frameRate > 0.0 ? frames / frameRate : 0.0; }
};

constexpr double ChromaLowestHz = 55.0, ChromaBassToHz = 260.0;
constexpr double ChromaNotesFromHz = 130.0, ChromaHighestHz = 2100.0;
// A peak within this many semitones of 3 or 5 times a louder peak's frequency is its overtone,
// and counts this much.
constexpr double ChromaOvertoneWithin = 0.35;
constexpr float ChromaOvertoneCounts = 0.3f;
constexpr int ChromaMedianHalfFrames = 3;  // "what stays": the median of 7 frames (0.35 s)

// Frame f is centred on f / frameRate seconds. Empty (frames 0) when the sound is shorter than
// a second, or cancelled.
Chromagram chromagram(const std::vector<float>& mono, double sampleRate, const Progress& progress = {});

// The mean of `rows` from `fromSeconds` to `toSeconds` (the frames centred in there; the
// nearest one when none is), scaled so its largest is 1 (all 0 when silent).
Chroma chromaBetween(const std::vector<Chroma>& rows, double frameRate, double fromSeconds, double toSeconds);

// "C", "C#", ... "B".
const char* noteName(int pitchClass) noexcept;

} // namespace analysis
