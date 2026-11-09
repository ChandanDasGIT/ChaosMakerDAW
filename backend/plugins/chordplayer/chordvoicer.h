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
// The Chords track's three registers for a strip's chord area (zone 0 low, 1 middle, 2 high),
// an octave apart and set by the chord's root: two octaves up from the root in octave 3, 4 or
// 5 (C: C3-C5, C4-C6, C5-C7; G: G3-G5, G4-G6, G5-G7).
constexpr Range zoneRange(int zone, int rootPitchClass) noexcept
{
    const int root = 48 + 12 * (zone < 0 ? 0 : zone > 2 ? 2 : zone) + ((rootPitchClass % 12) + 12) % 12;
    return Range{root, root + 24};
}
// The Chords track's bass note is in octave 2 (C2 .. B2); the rest of the bass part (third,
// fifth, octave) stays within the octave above it: C2 E2 G2 C3 for C, G2 B2 D3 G3 for G.
constexpr Range ChordsBass{36, 48};
constexpr Range bassRange(int bassPitchClass) noexcept
{
    const int low = ChordsBass.low + ((bassPitchClass % 12) + 12) % 12;
    return Range{low, low + 12};
}

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
