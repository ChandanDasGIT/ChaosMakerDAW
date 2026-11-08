#pragma once

#include "midi/midinote.h"
#include "plugins/chordplayer/chordsettings.h"

#include <vector>

// "Convert to notes" for a Chords clip: plays the clip's strip presses (notes on the trigger
// keys, clip-relative beats) through a ChordPlayer offline, with the transport running from the
// clip's start, and returns the notes it played (clip-relative beats, cut at the clip's end).
// Notes on other keys pass through as they are. Pure C++; any thread.
namespace chords {

std::vector<MidiNote> renderClip(const Settings& settings, const std::vector<MidiNote>& clipNotes,
                                 double clipStartBeat, double clipLengthBeats,
                                 double bpm, int numerator, int denominator);

} // namespace chords
