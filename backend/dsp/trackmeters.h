#pragma once
#include "dsp/peakmeter.h"
#include <array>

// Audio writes peaks; the UI consumes them. Snapshot ownership keeps these alive
// until both the playback and monitoring paths have released a removed track.
struct TrackMeters {
    std::array<PeakMeter, 2> input, output;
    PeakMeter raw, preFx;
    void clearClips() noexcept {
        for (auto& meter : input) meter.clearClip();
        for (auto& meter : output) meter.clearClip();
        raw.clearClip(); preFx.clearClip();
    }
};
