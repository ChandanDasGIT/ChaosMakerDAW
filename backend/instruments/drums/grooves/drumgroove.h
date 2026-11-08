#pragma once

#include "midi/midinote.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Drum grooves: ready-made beats for the Drums and Electronic Drums tracks. A groove is a
// pattern of hits on the drum keys both kits share (drummap: kick 60, snare 62, hi-hats
// 66 / 68 / 70, toms 65 / 67 / 69, side stick 61), so one groove plays on either kit with
// that kit's own sound. Times are in beats (quarter notes), never seconds, so a groove
// follows the project's tempo by itself. One bar is numerator * 4 / denominator beats
// (3/4 -> 3, 4/4 -> 4, 7/8 -> 3.5).
namespace drumgroove {

struct Groove {
    std::string name;
    int numerator = 4;
    int denominator = 4;
    int bars = 1;                         // how many bars the pattern spans before it repeats
    std::vector<MidiNote> notes;          // beats from the groove's start, 0 .. lengthBeats()
    bool factory = false;                 // built in: cannot be renamed, replaced or deleted

    double barBeats() const noexcept { return numerator * 4.0 / denominator; }
    double lengthBeats() const noexcept { return bars * barBeats(); }
    std::string meterText() const { return std::to_string(numerator) + "/" + std::to_string(denominator); }
};

constexpr int MaxBars = 16;               // a groove's own length
constexpr int MaxRepeatBars = 64;         // what one insert may fill
constexpr std::size_t MaxNotes = 4096;
constexpr std::size_t MaxNameLength = 48;

// The built-in grooves, two per time signature: 3/4, 4/4 and 7/8.
const std::vector<Groove>& factoryGrooves();

// A meter a groove may use: numerator 1..32, denominator 1, 2, 4, 8, 16 or 32.
bool validMeter(int numerator, int denominator) noexcept;
// A name a user groove may take: 1..MaxNameLength characters, not only spaces, no control
// characters and no path separators. Returns the name with outer spaces removed, or nothing.
std::optional<std::string> cleanName(std::string_view name);

// The groove's notes for `bars` bars from beat 0: the pattern repeats and the last copy is cut
// at the end. Note ids run 1, 2, 3 ... Nothing for bars outside 1..MaxRepeatBars.
std::vector<MidiNote> repeated(const Groove& groove, int bars);

// A groove made from a clip's notes (clip-relative beats) under the project's meter: its length
// is the clip's, rounded to whole bars (at least one); notes starting outside it are left out.
// Nothing (and `error` says why) when the meter or name is not valid, the clip is longer than
// MaxBars bars or no note is left.
std::optional<Groove> fromClip(std::string_view name, const std::vector<MidiNote>& clipNotes,
                               double clipLengthBeats, int numerator, int denominator,
                               std::string& error);

} // namespace drumgroove
