#pragma once

#include <cstdint>
#include <vector>

// Round-trip latency measurement: a known click is played out and recorded back through a
// loopback cable; the delay is where the click best matches the recording (cross-correlation).
// Used by tools/latencytest. Offline maths only: no audio device here.
namespace latencymeasure {

// A short click with a sharp correlation peak: a Hann-windowed 1-8 kHz chirp, ~2.7 ms long.
std::vector<float> makeClick(double sampleRate);

struct Result {
    bool found = false;
    std::int64_t delaySamples = 0;  // where the click starts in the recording
    double confidence = 0.0;        // normalised correlation at the peak, 0..1
    bool inverted = false;          // the click came back upside down
};

// Looks for `click` in `recorded` at delays 0..maxDelay (clipped to what fits; < 0 = any).
// found is false when nothing matches clearly: a weak peak (confidence < 0.5) or one that
// does not stand out from the rest of the correlation.
Result findDelay(const std::vector<float>& click, const std::vector<float>& recorded,
                 std::int64_t maxDelay = -1);

}
