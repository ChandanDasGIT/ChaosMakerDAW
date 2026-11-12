#pragma once
#include "instruments/bass/basssettings.h"

#include <array>

// Where on the neck a note is played. Plain functions, no state: BassGuitar keeps the hand
// position and which strings hold a key, and asks here.
namespace bass {

struct Position {
    int string = 0;                                // 0 = E .. 3 = G
    int fret = 0;                                  // 0 = open .. FretCount
};

// A key moved by octaves into LowestKey..HighestKey (a bass cannot play outside it).
int foldIntoRange(int key) noexcept;

// True when `key` is one of the three keyswitches (C0, D0, E0); the style it selects goes to
// `style`.
bool keyswitchStyle(int key, Style& style) noexcept;

// The fret `key` sits at on `string`, or -1 when that string cannot play it.
int fretOn(int string, int key) noexcept;

// The string for a new note, as a player's left hand would choose: of the strings that can
// play it and are not holding a key, the one nearest the hand's fret (`handFret`), with open
// strings as near as any and frets above the 12th a little further. When every string that
// can play it is held, the one whose key went down first (`heldSince`, lower = older) is
// taken. `held[s]` says string s holds a key. `key` must already be in range.
Position choose(int key, const std::array<bool, StringCount>& held,
                const std::array<unsigned long long, StringCount>& heldSince, double handFret) noexcept;

} // namespace bass
