#pragma once
#include "recording/takerecorder.h"
#include "recording/miditakerecorder.h"
#include <atomic>
#include <vector>

// UI owns the recorders and group until released and all disk workers are done.
// Entries are immutable after posting; active is touched only by the audio thread.
struct RecordingGroup {
    struct Entry {
        int trackId = -1, inputChannel = -1;
        TakeRecorder* recorder = nullptr;
        MidiTakeRecorder* midiRecorder = nullptr;
        bool active = true;
    };
    std::vector<Entry> entries;
    std::atomic<bool> released{false};
    std::atomic<bool> inputLost{false};
    std::atomic<bool> captureFailed{false};
};
