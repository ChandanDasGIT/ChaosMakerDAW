#pragma once

#include <string>

// MIDI note naming for editors. MIDI 60 = C4 (middle C), sharps only: C, C#, D ... B.
// Pure C++, no Qt. Notes outside 0..127 have no name.
namespace notenames {
bool isValid(int note) noexcept;
// 0 = C ... 11 = B; -1 for invalid notes.
int pitchClass(int note) noexcept;
// "C4", "C#4", "A-1" (note 9); empty for invalid notes.
std::string name(int note);
// Black piano key (C#, D#, F#, G#, A#).
bool isBlackKey(int note) noexcept;
// First note of an octave (every C): the piano roll marks these rows and keys.
bool isOctaveStart(int note) noexcept;
// Name and number for readouts, e.g. "C4 60"; empty for invalid notes.
std::string readout(int note);
}
