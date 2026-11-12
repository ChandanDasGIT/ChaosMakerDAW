#include "instruments/bass/bassfretboard.h"

#include <cmath>
#include <limits>

namespace bass {

int foldIntoRange(int key) noexcept
{
    while (key < LowestKey) key += 12;
    while (key > HighestKey) key -= 12;
    return key;
}

bool keyswitchStyle(int key, Style& style) noexcept
{
    switch (key) {
    case FingerKeyswitch: style = Style::Finger; return true;
    case SlapKeyswitch: style = Style::Slap; return true;
    case MuteKeyswitch: style = Style::Mute; return true;
    default: return false;
    }
}

int fretOn(int string, int key) noexcept
{
    if (string < 0 || string >= StringCount) return -1;
    const int fret = key - OpenKeys[string];
    return fret >= 0 && fret <= FretCount ? fret : -1;
}

Position choose(int key, const std::array<bool, StringCount>& held,
                const std::array<unsigned long long, StringCount>& heldSince, double handFret) noexcept
{
    // How far the hand has to reach for this fret.
    const auto reach = [handFret](int fret) {
        if (fret == 0) return 0.0;                     // an open string needs no hand
        return std::abs(fret - handFret) + (fret > 12 ? 0.5 * (fret - 12) : 0.0);
    };

    Position best{-1, -1};
    double bestReach = std::numeric_limits<double>::infinity();
    for (int s = 0; s < StringCount; ++s) {
        const int fret = fretOn(s, key);
        if (fret < 0 || held[std::size_t(s)]) continue;
        const double r = reach(fret);
        if (r < bestReach) { bestReach = r; best = {s, fret}; }   // ties: the lower string
    }
    if (best.string >= 0) return best;

    // Every string that can play it holds a key: take the oldest.
    unsigned long long oldest = std::numeric_limits<unsigned long long>::max();
    for (int s = 0; s < StringCount; ++s) {
        const int fret = fretOn(s, key);
        if (fret < 0) continue;
        if (heldSince[std::size_t(s)] < oldest) { oldest = heldSince[std::size_t(s)]; best = {s, fret}; }
    }
    if (best.string >= 0) return best;
    // Out of range (the caller folds first): the nearest end of the neck.
    return key < OpenKeys[1] ? Position{0, 0} : Position{StringCount - 1, FretCount};
}

} // namespace bass
