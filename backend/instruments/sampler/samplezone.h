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
    float hold = 0.0f;                                    // seconds at full level after the attack
    std::shared_ptr<const VelocityCurve> velocityCurve;   // none: gain is (velocity / 127)^2

    // A 2-pole low-pass on the sample (SFZ cutoff with fil_type lpf_2p); 0 Hz: no filter.
    float cutoffHz = 0.0f;
    float resonanceDb = 0.0f;                             // peak at the cutoff, 0 dB = Butterworth

    // Voices of one key: with notePolyphony n, a new voice of this zone's group on the same
    // key fades the older ones past n (a piano string struck again). 0: no limit.
    int notePolyphony = 0;
    float offSeconds = 0.005f;                            // how long this zone takes to fade when cut (off_time)

    // The sustain pedal the classic way: while controller sustainCc is at 64 or more, a let-go
    // key keeps sounding until the pedal comes up.
    int sustainCc = 64;

    // A controller the player moves while notes sound (the loader's live controllers, e.g. the
    // sustain pedal). liveCc >= 0: the zone only starts while that controller is within
    // [liveLo, liveHi] (a string-resonance layer with the pedal down).
    int liveCc = -1, liveLo = 0, liveHi = 127;
    // releaseCc >= 0: the release lasts release + releaseCcSeconds * curve[value of releaseCc]
    // (a pedal that lengthens the release); with dynamicRelease a release already under way
    // follows the controller as it moves (half pedalling).
    using ControllerCurve = std::array<float, 128>;
    int releaseCc = -1;
    float releaseCcSeconds = 0.0f;
    std::shared_ptr<const ControllerCurve> releaseCcCurve; // none: linear
    bool dynamicRelease = false;

    // Release seconds for a controller value (0..127) of releaseCc.
    float releaseFor(int controller) const noexcept
    {
        if (releaseCc < 0) return release;
        const int v = controller < 0 ? 0 : controller > 127 ? 127 : controller;
        const float shape = releaseCcCurve ? (*releaseCcCurve)[std::size_t(v)] : float(v) / 127.0f;
        const float seconds = release + releaseCcSeconds * shape;
        return seconds > 0.0f ? seconds : 0.0f;
    }
};
