#pragma once

#include "engine/enginesnapshot.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// The offline render of the song for export (restMap F2): a playback snapshot played through an
// AudioEngine of its own, block by block and as fast as the computer allows, from `start` for
// `length` samples plus a tail (reverbs and delays ring out). The metronome is never in it.
// The snapshot's processors must not be used by anything else meanwhile (the live engine is
// suspended).
namespace songrender {

struct Options {
    double sampleRate = 48000.0;
    std::int64_t start = 0;          // samples
    std::int64_t length = 0;         // samples
    std::int64_t tail = 0;           // samples after the end
    double bpm = 120.0;
    float masterVolume = 1.0f;
    bool masterMuted = false;
    int blockFrames = 512;
};

// Two channels of `length + tail` samples. `progress` gets 0..1 now and then; returning false
// cancels (the result is then two empty channels).
std::vector<std::vector<float>> render(std::unique_ptr<EngineSnapshot> snapshot, const Options& options,
                                       const std::function<bool(double)>& progress = {});

// The loudest sample to `peakDb` (e.g. 0 or -1 dBFS); silence stays silence.
void normalise(std::vector<std::vector<float>>& channels, float peakDb);

} // namespace songrender
