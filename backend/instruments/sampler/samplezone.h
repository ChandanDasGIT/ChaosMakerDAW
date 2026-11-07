#pragma once
#include "core/audiodata.h"

#include <cstdint>
#include <memory>

// One region of an SFZ file.
struct SampleZone {
    std::shared_ptr<const AudioData> audio;
    int loKey = 0, hiKey = 127, rootKey = 60;
    int loVelocity = 0, hiVelocity = 127;
    int roundRobinLength = 1, roundRobinPosition = 1;
    int group = 0, offBy = 0;                             // an open hi-hat is choked by the closed one
    bool releaseTrigger = false;
    bool oneShot = false;                                 // note-off is ignored: plays to the end (drums)
    bool looping = false;
    std::int64_t loopStart = 0, loopEnd = 0;
    float tuneCents = 0.0f, volumeDb = 0.0f, pan = 0.0f;
    float attack = 0.0f, decay = 0.0f, sustain = 1.0f, release = 0.05f;
};
