#pragma once
#include <cstdint>
class TakeRecorder;

struct EngineCommand {
    enum class Type : std::uint8_t { Play, Pause, Stop, Seek, SetLoop, ClearLoop, StartRecording, StopRecording, SetCleanMonitor,
                                     SetMetronome, NoteFromUi, TestTone, SetCleanMonitorGain };
    Type type = Type::Stop;
    std::int64_t a = 0;
    std::int64_t b = 0;
    float value = 0.0f;
    TakeRecorder* recorder = nullptr;
};

struct EngineEvent {
    enum class Type : std::uint8_t { Position, Xrun, RecordingStarted, RecordingOverflow, LoopWrapped };
    Type type = Type::Position;
    std::int64_t samplePosition = 0;
};
