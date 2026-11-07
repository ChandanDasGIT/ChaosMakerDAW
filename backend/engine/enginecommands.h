#pragma once
#include <cstdint>
class TakeRecorder;
struct RecordingGroup;

struct EngineCommand {
    enum class Type : std::uint8_t { Play, Pause, Stop, Seek, SetLoop, ClearLoop, StartRecording, StopRecording, SetCleanMonitor,
                                     SetMetronome, NoteFromUi, TestTone, SetCleanMonitorGain, StartRecordingGroup,
                                     SetStopAtEnd }; // SetStopAtEnd: a = 1 stops playback at the arrangement end
    Type type = Type::Stop;
    std::int64_t a = 0; // StartRecordingGroup: count-in/pre-roll duration in samples.
    std::int64_t b = 0;
    float value = 0.0f;
    TakeRecorder* recorder = nullptr;
    RecordingGroup* group = nullptr;
};

struct EngineEvent {
    enum class Type : std::uint8_t { Position, Xrun, RecordingStarted, RecordingOverflow, LoopWrapped };
    Type type = Type::Position;
    std::int64_t samplePosition = 0;
};
