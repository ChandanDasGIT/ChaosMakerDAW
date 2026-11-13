#pragma once

#include "analysis/beattracker.h"
#include "analysis/chromagram.h"
#include "analysis/meterdetect.h"
#include "analysis/progress.h"
#include "plugins/chordplayer/chordsymbol.h"

#include <optional>
#include <string>
#include <vector>

// The chord analyzer (restMap G21): from a song to its chords over time and its key.
//
//   analyseChords(): the note names over time (chromagram) -> the beats (the drum analyzer's
//   tracker; half-second slots when no beat is found) -> the notes of each beat -> every beat
//   compared with every chord's shape (the chord's notes with their overtones, and its root in
//   the bass) -> the likeliest run of chords, which pays for each change so chords do not
//   flicker (Viterbi) -> the key from how long each chord sounds -> the run once more, the
//   key's own chords a little likelier.
//
// Chords: major and minor on all twelve notes, and (ChordOptions::sevenths) their sevenths: 7,
// maj7, m7, which must be clearly there to be named. "No chord" where the sound is nearly
// silent. A note held under every chord (a drone) is taken out first: it says nothing about
// which chord sounds.
//
// A chord chart to correct, not a transcription: good on clear pop / rock / folk chords, weaker
// on jazz chords, inversions and heavy distortion. Changes fall on beats. Hand-written, no
// learning.
namespace analysis {

enum class ChordQuality { None, Major, Minor, Dominant7, Major7, Minor7 };

struct ChordSpan {
    double fromSeconds = 0.0, toSeconds = 0.0;
    int fromSlot = 0, slotCount = 0;           // beats (or half-second slots)
    int root = -1;                         // pitch class, -1: no chord
    ChordQuality quality = ChordQuality::None;
    std::string name;                      // "Gm", "Bb", "C7"; "" for no chord
    float sureness = 0.0f;                 // 0..1: how far ahead of the next best chord it was
};

struct ChordOptions {
    bool sevenths = true;
};

struct ChordAnalysis {
    double seconds = 0.0;                  // the analysed sound's length
    double tuningCents = 0.0;
    bool hasBeat = false;                  // false: half-second slots, no tempo
    double bpm = 0.0;                      // quarter notes per minute
    int numerator = 4, denominator = 4;
    double firstBarSeconds = 0.0;          // a bar line (may be < 0)
    double barSeconds = 0.0;               // 0 without a beat
    chords::Key key;
    std::vector<double> slotSeconds;       // each slot's start; one more at the end
    std::vector<Chroma> slotNotes, slotBass;
    std::vector<ChordSpan> chords;         // in time order, joined, covering all slots
};

// Nothing when cancelled, or when the sound is shorter than ChordMinSeconds.
constexpr double ChordMinSeconds = 4.0;
std::optional<ChordAnalysis> analyseChords(const std::vector<float>& mono, double sampleRate, const ChordOptions& options = {},
                                           const Progress& progress = {});

// The chords again with other options, without analysing the sound again.
void decodeChords(ChordAnalysis& analysis, const ChordOptions& options);

// The chord names by how long they sound in all, the longest first (no chord left out).
std::vector<std::string> commonChords(const ChordAnalysis& analysis);

// A chord's name in a key's spelling (flats in flat keys): "Bb", "F#m", "G7", "Cmaj7", "Am7".
std::string chordName(int root, ChordQuality quality, const chords::Key& key);

} // namespace analysis
