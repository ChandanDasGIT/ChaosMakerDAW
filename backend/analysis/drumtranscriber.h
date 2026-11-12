#pragma once

#include "analysis/beattracker.h"
#include "analysis/drumnmf.h"
#include "analysis/meterdetect.h"
#include "analysis/onsets.h"
#include "analysis/progress.h"
#include "midi/midinote.h"

#include <optional>
#include <vector>

// The drum pattern analyzer (restMap G20): from a recording (a drum loop or a whole song) to a
// drum pattern on the keys both drum kits share, with its tempo and time signature.
//
//   analyseDrums(): spectrogram -> drum / other separation -> onsets -> each hit sorted into
//   kick / snare / hi-hat by where its energy rises -> beats -> meter -> the hits snapped to
//   the grid (16ths or triplets, or eighths in a compound meter, whichever fits the hits).
//   retempo(): the same hits with the tempo doubled or halved (when the beat was counted at
//   the wrong speed), without analysing the sound again.
//
// Two ways, by how much of the sound is harmonic (Envelopes::drumsOnly):
//   drums only (a loop or stem): the kit's own kick, snare and hi-hat sounds are learnt from
//     the recording first (kitshapes), the tempo is halved when the snare's backbeat says so,
//     and beats that fell between the kick and snare move onto them;
//   a song: generic drum shapes, as the other instruments would be learnt as drums.
// A ride or crash comes out as a hi-hat (the kits' shared keys have no cymbals); toms are left
// out. Measured with tools/drumbench against what drummers played (restMap G20): the kick is
// good, the snare and hats are a draft to fix in the piano roll, ghost notes are mostly missed.
namespace analysis {

// The drum keys (drummap / electromap): the same on both kits.
constexpr int KickKey = 60;
constexpr int SnareKey = 62;
constexpr int ClosedHatKey = 66;
constexpr int OpenHatKey = 70;

// One hit found in the sound, before the grid.
struct DrumHit {
    double seconds = 0.0;                  // from the start of the analysed sound
    int key = KickKey;
    float strength = 0.0f;                 // the hit's rise against the typical one of its drum
};

struct DrumPattern {
    double bpm = 120.0;                    // quarter notes per minute (the project's BPM)
    int numerator = 4, denominator = 4;
    double firstBarSeconds = 0.0;          // where bar 1 starts, from the sound's start (may be < 0)
    double secondsPerQuarter = 0.5;
    double gridQuarters = 0.25;            // the grid the hits were snapped to
    int bars = 0;
    std::vector<MidiNote> notes;           // beats (quarter notes) from bar 1's start, in time order
    int kicks = 0, snares = 0, hats = 0;

    double barQuarters() const noexcept { return numerator * 4.0 / denominator; }
};

struct DrumAnalysis {
    double seconds = 0.0;                  // the analysed sound's length
    Envelopes envelopes;
    std::vector<DrumHit> hits;
    Beat beat;
    Meter meter;
    DrumPattern pattern;
};

// Nothing when cancelled, or when the sound is shorter than MinSeconds.
constexpr double MinSeconds = 4.0;
std::optional<DrumAnalysis> analyseDrums(const std::vector<float>& mono, double sampleRate, const Progress& progress = {});

// The analysis again at `factor` (2 or 0.5) times the beat's tempo, within the tracker's range;
// nothing when that tempo is outside it.
std::optional<DrumAnalysis> retempo(const DrumAnalysis& analysis, double factor);

// The parts of the analysis, for tests and the BPM analyzer. pickHits: each drum's hits from
// its rise in `envelopes` (kick, snare, hat): the peaks that stand out from their surroundings
// and reach a part of that drum's typical hit.
std::vector<DrumHit> pickHits(const Envelopes& envelopes);
DrumPattern transcribe(const std::vector<DrumHit>& hits, const Beat& beat, const Meter& meter);

// The `count` bars that repeat most through the pattern (each compared with every other window
// of that many bars), from 0: a groove made of the song's most typical bars. `startBar` says
// which bars were taken (0-based). All of it when the pattern is not longer.
std::vector<MidiNote> typicalBars(const DrumPattern& pattern, int count, int* startBar = nullptr);

} // namespace analysis
