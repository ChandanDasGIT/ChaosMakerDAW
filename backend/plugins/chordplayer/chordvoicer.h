#pragma once

#include "plugins/chordplayer/chordsymbol.h"

#include <array>
#include <span>
#include <vector>

// How a pianist would place a chord: the right hand in close position somewhere in a
// comfortable range, choosing the inversion that moves least from the chord before (voice
// leading: each finger to its counterpart), and the left hand's bass note low, near the bass
// before. At most four notes in the right hand: a ninth chord leaves out its fifth.
namespace chords {

struct Range {
    int low = 55;                         // G3
    int high = 79;                        // G5
};

constexpr Range RightHand{55, 79};
constexpr Range BassRange{36, 52};        // C2 .. E3

// A right-hand voicing without heap memory (the chord player builds these on the audio thread).
struct Voicing {
    std::array<int, 4> notes{};           // ascending MIDI notes, the first `count` used
    int count = 0;
};

// The chord's tones as pitch classes in stacking order (root first), at most four.
std::vector<int> chordTones(const Chord& chord);

// The core, allocation-free: the voicing of `pitchClasses` (at most four, root first) closest
// to `previous` (count 0: none, then it sits around the middle of the range).
Voicing voiceTones(std::span<const int> pitchClasses, const Voicing& previous, Range range = RightHand) noexcept;

// The right hand's notes for `chord`, ascending. `previous` (the notes played before, any
// order; empty for the first chord) steers the choice.
std::vector<int> voice(const Chord& chord, const std::vector<int>& previous, Range range = RightHand);

// The bass note of pitch class `pitchClass` in `range`, as close as it can be to
// `previousBass` (or the middle of the range when it is -1). -1 if none fits.
int bassNote(int pitchClass, int previousBass = -1, Range range = BassRange) noexcept;
// The chord's bass (its root, or its slash bass).
int bassNote(const Chord& chord, int previousBass = -1, Range range = BassRange);

// Total movement between two voicings: each note to the nearest note of the other, both ways.
int movement(const std::vector<int>& from, const std::vector<int>& to);

} // namespace chords
