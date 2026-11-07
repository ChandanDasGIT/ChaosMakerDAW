#pragma once

#include <span>
#include <string_view>

// The acoustic drum kit's keys (Gogodze Phu Vol II) and the names the piano roll shows for
// them. The General MIDI drum layout two octaves up, so the computer keyboard (KEYS) reaches
// every part at its default octave: kick on 'e' (Sa, C4 = 60), snare on 'r', toms on y/u/i,
// hi-hats closed / pedal / open on 7/8/9, the extra hi-hats just below the kick (Tab, 1, q).
// 73 and 75 stay free for a crash and a ride (General MIDI 49 and 51, two octaves up).
//
// The kit plays its hi-hat articulations on one key (46) and picks them with MIDI CC 4 (pedal
// position). The piano roll has no controller lanes, so each articulation gets a key of its
// own: the kit is read once per articulation with CC 4 set, and that pass's key-46 zones (the
// hits and the silent hits that choke other hats) move to the articulation's key.
namespace drummap {

struct Pad {
    int key;
    std::string_view name;
    std::string_view id;                  // for the kit picture: "kick", "hihatOpen" ...
};

// Every key the kit plays, from low to high.
std::span<const Pad> pads() noexcept;

// The part on `key`, or "" when the kit has nothing there.
std::string_view nameOf(int key) noexcept;
// The key of the part with this id, or -1.
int keyOf(std::string_view id) noexcept;

// One read of the kit: CC 4 at `hihatPedal`; zones on kit key `fromKey` play on `toKey`.
// A pass with fromKey == toKey == -1 keeps every key no other pass takes, KitOffset higher.
struct Pass {
    int hihatPedal;                       // CC 4, 0..127
    int fromKey, toKey;
};

std::span<const Pass> passes() noexcept;

// How far the kit's own keys move up (General MIDI 36 -> 60).
constexpr int KitOffset = 24;
// The kit's own keys that passes() moves (they are not kept where they are).
constexpr int KitHihatKey = 46;           // closed .. open by CC 4
constexpr int KitTightHihatKey = 42;

} // namespace drummap
