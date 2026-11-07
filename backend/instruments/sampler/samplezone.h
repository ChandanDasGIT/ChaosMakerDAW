#pragma once
#include "core/audiodata.h"

#include <array>
#include <cstdint>
#include <memory>

// One region of an SFZ file.
struct SampleZone {
    using VelocityCurve = std::array<float, 128>;         // gain for each velocity, 0..1

    std::shared_ptr<const AudioData> audio;
    int loKey = 0, hiKey = 127, rootKey = 60;
    int loVelocity = 0, hiVelocity = 127;
    int roundRobinLength = 1, roundRobinPosition = 1;
    int group = 0, offBy = 0;                             // an open hi-hat is choked by the closed one
    bool silent = false;                                  // sample=*silence: chokes its group, sounds nothing, needs no audio
    bool releaseTrigger = false;
    bool oneShot = false;                                 // note-off is ignored: plays to the end (drums)
    bool looping = false;
    std::int64_t loopStart = 0, loopEnd = 0;
    std::int64_t offset = 0;                              // first sample frame played
    float delaySeconds = 0.0f;                            // silence before the sample starts (mic distance)
    float tuneCents = 0.0f, volumeDb = 0.0f, pan = 0.0f;
    float attack = 0.0f, decay = 0.0f, sustain = 1.0f, release = 0.05f;
    std::shared_ptr<const VelocityCurve> velocityCurve;   // none: gain is (velocity / 127)^2
};
