#pragma once
#include "instruments/electrodrums/electrovoices.h"

#include <span>
#include <string_view>

// The electronic drum kit's keys and the names the piano roll shows for them. The acoustic kit's
// keys (drummap) so a pattern plays on either kit, plus the parts only this kit has: clap on 63
// (General MIDI 39, two octaves up), crash on 73 and ride on 75 (the keys kept free for them).
// The acoustic kit's extra hi-hats (55..57) and snare edge (64) play the nearest drum here.
namespace electromap {

struct Pad {
    int key;
    electrodrums::Drum drum;
    std::string_view name;
    double decayScale = 1.0;              // times the drum's decay: a tight or half-open hat
};

// Every key the kit plays, from low to high.
std::span<const Pad> pads() noexcept;

// The pad on `key`, or nullptr when the kit has nothing there.
const Pad* find(int key) noexcept;
// The name on `key`, or "".
std::string_view nameOf(int key) noexcept;

} // namespace electromap
