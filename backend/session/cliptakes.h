#pragma once

#include "midi/midinote.h"

#include <functional>
#include <string>
#include <vector>

// Takes (restMap G11): one clip can hold several recordings of the same stretch (loop passes,
// a punch-in over older audio) and plays one of them, the active take. The clip's own poolId,
// sourceOffset, reversed and notes ARE the active take, so playback, waveforms, fades, clip
// effects and export never look at the others. The others are kept here beside it.
//
// Where a take's content sits: its "origin" is the project sample where its file's frame 0
// (a pattern: its beat 0) would play, clip.start - sourceOffset / ratio (ratio = the file's
// rate / the project's). Each take keeps its origin relative to the active take's (`lead`), so
// moving, trimming and splitting a clip never touches the takes; switching works the offset
// out again. Splitting a clip gives each piece all the takes: pick one per piece (comping).
struct ClipTake {
    std::string poolId;              // its audio (a pattern take: "Clip N", no media)
    bool reversed = false;           // reads the reversed copy, like AudioClip::reversed
    double lead = 0.0;               // project samples: its origin minus the active take's
    std::vector<MidiNote> notes;     // a pattern take's notes, origin-relative beats
    bool operator==(const ClipTake&) const = default;
};

struct ClipTakes {
    // Empty: the clip has one take (its own audio). Otherwise every take in order; the entry
    // at `active` is an empty placeholder (the clip's own fields hold that take).
    std::vector<ClipTake> list;
    int active = 0;
    bool operator==(const ClipTakes&) const = default;
};

struct AudioClip;

namespace cliptakes {

// A take's file rate / the project's rate (1 for a pattern or unknown audio).
using RatioOf = std::function<double(const std::string& poolId, bool reversed)>;

int count(const AudioClip& clip) noexcept;          // 1 for a clip without takes
int active(const AudioClip& clip) noexcept;         // 0 for a clip without takes

// Gives the clip these takes: `takes` holds every take in order; the one at `active` is the
// clip's own (its entry is replaced by the placeholder). Fewer than two: no takes.
void set(AudioClip& clip, std::vector<ClipTake> takes, int active);

// Plays take `index` from now on. False, and no change, for a bad index or when that take's
// content starts after the clip does (its offset would be below 0).
bool activate(AudioClip& clip, int index, const RatioOf& ratioOf);

// Removes take `index`. Removing the active one: the next take that can play here plays instead
// (after the last: from the first). False for a clip with one take, a bad index, or when no
// other take can play.
bool remove(AudioClip& clip, int index, const RatioOf& ratioOf);

// Keeps only the active take.
void cropToActive(AudioClip& clip) noexcept;

// The active take's origin moved by `delta` project samples while its content stayed (e.g.
// reversing it): the other takes keep their place.
void activeOriginMoved(AudioClip& clip, double delta) noexcept;

} // namespace cliptakes
