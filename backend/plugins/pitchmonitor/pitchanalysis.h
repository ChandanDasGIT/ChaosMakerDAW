#pragma once

#include "dsp/pitchtracker.h"

#include <atomic>
#include <cstdint>
#include <vector>

class TrackList;

// The pitch of a track's (or one clip's) recorded audio over the song, for the Pitch Monitor
// page (restMap G18). collect() reads the audio on the thread that owns the TrackList (the
// clips as they play: trims, offsets, the active take, reversed clips, clip gain; no effects);
// analyse() can then run on any thread.
namespace pitchanalysis {

// One stretch of audio in song samples: the clips' spans, overlapping ones merged.
struct Segment {
    std::int64_t start = 0;
    std::vector<float> mono;
};

// One 10 ms frame at its song sample (the page turns it into beats, so a tempo change needs no
// new analysis). midi 0: no pitch there (a gap in the line).
struct Point {
    std::int64_t sample = 0;
    float midi = 0.0f;
    float clarity = 0.0f;
};

// Spans longer than this are read in pieces (TrackList::ownerAudio reads at most 10 minutes).
constexpr double MaxPieceSeconds = 300.0;

std::vector<Segment> collect(const TrackList& tracks, int effectsId);
// Every segment's frames in song order, with a gap point after each segment. Empty when
// cancelled.
std::vector<Point> analyse(const std::vector<Segment>& segments, double sampleRate,
                           const std::atomic<bool>* cancel = nullptr);

} // namespace pitchanalysis
