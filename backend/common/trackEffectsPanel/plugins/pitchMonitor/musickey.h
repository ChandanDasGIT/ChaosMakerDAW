#pragma once

#include <span>
#include <string>
#include <string_view>

// Keys and scales for measuring a sung or played pitch (restMap G18): which notes are in the
// key, and the in-key note nearest a pitch with how many cents it is off. A key is a tonic
// (0 = C ... 11 = B) and a scale. Chromatic: every note is in the key.
namespace musickey {

enum class Scale { Major, Minor, HarmonicMinor, MajorPentatonic, MinorPentatonic, Chromatic };
constexpr int ScaleCount = 6;

std::span<const std::string_view> tonicNames();     // "C", "C#" ... "B" (12)
std::span<const std::string_view> scaleNames();     // "Major", "Minor" ... in Scale's order
std::string keyName(int tonic, Scale scale);        // "A minor", "C# major", "Chromatic"

// The note's pitch class is in the key. Any MIDI number (negative too).
bool inKey(int note, int tonic, Scale scale) noexcept;

struct Nearest {
    int note = -1;                        // the nearest in-key MIDI note
    double cents = 0.0;                   // the pitch minus that note: + sharp, - flat
};
// `midi` is a fractional MIDI number (pitch::midiOf). Ties go to the lower note.
Nearest nearest(double midi, int tonic, Scale scale) noexcept;

} // namespace musickey
