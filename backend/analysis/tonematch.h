#pragma once

#include "analysis/progress.h"
#include "analysis/tonefeatures.h"
#include "instruments/guitar/presets/guitarsuitepreset.h"

#include <optional>
#include <string>
#include <vector>

// The guitar tone analyzer (restMap G19): from a recording of a guitar tone to a GuitarSuite
// pedalboard that sounds close to it.
//
//   matchTone(): the reference is measured (tonefeatures) -> chords or single notes ->
//   a dry guitar of that kind (dryguitar) is played through candidate pedalboards, offline,
//   and each result measured and compared:
//     1. the gain: amp channel (Clean / Crunch / High Gain), its drive, an Overdrive in front
//        or not: mostly by how squashed the sound is;
//     2. the amp's tone controls and the cabinet (the built-in models, so the preset needs no
//        file), one control at a time, twice round;
//     3. the parametric EQ after the cabinet takes up what is still off, band by band.
//   Reverb (and a delay for a solo) by how long the reference rings on where the playing
//   stops; a noise gate in front. Settings far from the middle cost a little, so the result
//   does not end with every knob at its stop.
//
// The reference should be the guitar alone: drums, bass or a voice in it are measured as part
// of the tone. The preset fits the built-in dry guitar, a typical one: with hotter or thinner
// pickups the drive and tone controls want a nudge. A close starting point, not a clone.
// Hand-written, no learning.
namespace analysis {

enum class TonePlaying { Auto, Chords, Solo };

struct ToneMatchOptions {
    TonePlaying playing = TonePlaying::Auto;
};

struct ToneMatch {
    GuitarSuitePresetData preset;          // no name: the caller's
    ToneFeatures reference, matched;       // what was aimed at, and what the preset gives
    bool solo = false;                     // single notes (found, or as asked)
    int channel = 1;                       // 0 Clean, 1 Crunch, 2 High Gain
    bool overdrive = false;                // an Overdrive in front
    int cabinet = 3;                       // the Cabinet's model (1 1x12 Open, 2 2x12, 3 4x12 Closed)
    bool reverb = false, delay = false;
    double balanceErrorDb = 0.0;           // how far the colours are apart, 100 Hz .. 6.4 kHz
    int renders = 0;
};

// The reference needs at least a second of sound; nothing when it has less, or when cancelled.
constexpr double ToneMinSeconds = 2.0;
// At most this much of the reference is measured (from its start).
constexpr double ToneMaxSeconds = 60.0;
std::optional<ToneMatch> matchTone(const std::vector<float>& mono, double sampleRate, const ToneMatchOptions& options = {},
                                   const Progress& progress = {});

// "Clean", "Crunch", "High Gain".
const char* toneChannelName(int channel) noexcept;
// "1x12 Open", "2x12", "4x12 Closed".
const char* toneCabinetName(int model) noexcept;

} // namespace analysis
