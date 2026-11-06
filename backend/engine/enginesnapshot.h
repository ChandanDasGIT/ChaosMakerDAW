#pragma once
#include "core/audiodata.h"
#include "midi/midievent.h"
#include "processor/processor.h"
#include "transport/tempomap.h"
#include "dsp/trackmeters.h"

#include <cstdint>
#include <memory>
#include <vector>

struct ClipRender {
    const AudioData* audio = nullptr;
    std::int64_t start = 0;                               // on the timeline
    std::int64_t length = 0;
    std::int64_t sourceOffset = 0;                        // into the file
    std::int64_t fadeIn = 0;
    std::int64_t fadeOut = 0;
    float gain = 1.0f;
    bool effectsPrinted = true;
};

struct NoteRender {
    std::int64_t on = 0;
    std::int64_t off = 0;
    std::uint8_t key = 60;
    std::uint8_t velocity = 100;

    bool operator==(const NoteRender&) const = default;
};

struct TrackRender {
    std::shared_ptr<TrackMeters> meters;
    int trackId = 0;
    float gain = 1.0f;
    float panLeft = 1.0f, panRight = 1.0f;                // pan law per channel, times gain
    bool muted = false, soloed = false;
    std::vector<ClipRender> clips;                        // sorted by start
    std::vector<NoteRender> notes;                        // sorted by on
    std::vector<NoteRender> noteOffs;                     // sorted by off
    std::vector<Processor*> chain;                        // instrument first, then inserts
    const Parameter* volumeDb = nullptr;
    const Parameter* pan = nullptr;
    const Parameter* mute = nullptr;
    const Parameter* solo = nullptr;
    int inputChannel = -1;                                // -1: none
    bool armed = false;
    bool monitoring = false;
    std::vector<Processor*> monitorChain;
};

// Flat, finished, read-only once it has been published. The keepAlive lists are why the audio thread
// never destroys anything: the last owner of an old clip or plugin is an old snapshot, freed on the UI thread.
struct EngineSnapshot {
    bool multiMonitor = false;
    std::int64_t playbackEnd = -1; // Negative: no automatic endpoint, including an empty arrangement.
    std::shared_ptr<TrackMeters> monitorMeters;
    Processor* inputStage = nullptr;                    // optional per-track trim and EQ
    std::vector<TrackRender> tracks;
    std::vector<Processor*> masterChain;
    float masterGain = 1.0f;
    bool masterMuted = false;
    TempoMap tempo;
    std::vector<std::shared_ptr<const AudioData>> keepAudio;
    std::vector<std::shared_ptr<Processor>> keepProcessors;
};
