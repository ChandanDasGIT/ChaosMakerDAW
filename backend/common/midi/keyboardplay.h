#pragma once

// Playing notes from an on-screen keyboard (the virtual piano, the piano picture).
namespace keyboardplay {
// The velocity of a click `fraction` (0 top .. 1 bottom) down a key: from `lowest` at the top to
// 127 at the bottom, never under 30.
int clickVelocity(double fraction, int lowest);
// The note sent for a key when the track plays `shift` semitones higher (its scale); -1 when it
// falls outside 0..127 or the key is none (< 0).
int shiftedNote(int note, int shift);
}
