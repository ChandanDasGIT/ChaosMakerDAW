#pragma once

#include "recording/recordinggroup.h"
#include "recording/takerecorder.h"
#include "session/project.h"
#include "engine/enginecommands.h"
#include "transport/tempomap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class RecordingController {
public:
    using AttachTakeCallback = std::function<void(int trackId, std::int64_t startSample, std::shared_ptr<const AudioData> audio, bool printed)>;
    using AttachMidiTakeCallback = std::function<void(int trackId, std::int64_t startSample, std::int64_t lengthSamples, std::vector<MidiNote> notes)>;
    using LockTracksCallback = std::function<void(const std::vector<int>& lockedTracks)>;
    using UnlockCallback = std::function<void()>;
    using ChannelLookup = std::function<int(int trackId)>;
    using CommandPoster = std::function<bool(const EngineCommand& command)>;

    struct FinalizeResult {
        int completedTracks = 0;
        std::vector<std::string> errors;
        std::string message;
    };

    RecordingController();
    ~RecordingController();

    bool isBusy() const noexcept { return !m_takes.empty() || !m_midiTakes.empty(); }
    int primaryRecordingTrackId() const noexcept { return m_recordTrack; }
    std::vector<int> activeRecordingTrackIds() const;
    double recordStartBeat() const noexcept { return m_recordStartBeat; }
    void setRecordStartBeat(double beat) noexcept { m_recordStartBeat = beat; }
    std::size_t takeCount() const noexcept { return m_takes.size() + m_midiTakes.size(); }
    // Recording latency compensation (restMap G10), in project samples: audio takes are placed
    // this much earlier (their start may go below 0; TrackList::attachTake trims that part).
    // MIDI takes are not moved. Set before startRecording; it holds for that recording.
    void setLatencyCompensation(std::int64_t samples) noexcept { m_compensation = std::max<std::int64_t>(0, samples); }
    std::int64_t latencyCompensation() const noexcept { return m_compensation; }
    // The shift for a recording: the device's reported round trip plus the user's extra offset
    // (ms, -100..100, for what the report leaves out; tools/latencytest measures it), never
    // below 0; 0 when compensation is off.
    static std::int64_t compensationSamples(bool enabled, int roundTripFrames, double extraOffsetMs, double sampleRate) noexcept {
        if (!enabled || !(sampleRate > 0)) return 0;
        const double offset = std::isfinite(extraOffsetMs) ? std::clamp(extraOffsetMs, -100.0, 100.0) : 0.0;
        return std::max<std::int64_t>(0, std::max(0, roundTripFrames) + std::llround(offset * sampleRate / 1000.0));
    }

    // The waveform of the take being recorded on a track so far: `bins` (min, max) pairs; empty
    // when the track has no audio take running (UI thread).
    std::vector<std::pair<float, float>> livePeaks(int trackId, int bins, double from = 0.0, double to = 1.0) const;

    bool countInMessagePending() const noexcept { return m_countInMessagePending; }
    void clearCountInMessagePending() noexcept { m_countInMessagePending = false; }

    // Validation: checks whether tracks can be recorded
    static bool validateEligibility(
        const Project& project,
        bool audioReady,
        bool inputConnected,
        bool outputConnected,
        std::vector<int>& armedTracks,
        std::string& errorMessage);

    // Starts recording across armed tracks, allocates TakeRecorders and posts StartRecordingGroup
    bool startRecording(
        const Project& project,
        const std::string& recordingsFolder,
        double sampleRate,
        bool countInEnabled,
        double bpm,
        int numerator,
        int denominator,
        const ChannelLookup& channelLookup,
        const CommandPoster& commandPoster,
        const LockTracksCallback& lockCallback,
        std::string& outMessage);

    // Polls active takes, attaches progressively as each take recorder finishes,
    // and returns true if the entire group has been released and all takes are done.
    bool poll(
        bool isEngineRecording,
        std::int64_t engineRecordingStart,
        const TempoMap& tempo,
        const AttachTakeCallback& attachCallback,
        const AttachMidiTakeCallback& attachMidiCallback = nullptr);

    // Finalizes all takes, attaches any remaining audio/MIDI, unlocks tracks, and resets group state
    FinalizeResult completeTake(
        const AttachTakeCallback& attachCallback,
        const AttachMidiTakeCallback& attachMidiCallback,
        const UnlockCallback& unlockCallback);

    FinalizeResult completeTake(
        const AttachTakeCallback& attachCallback,
        const UnlockCallback& unlockCallback)
    {
        return completeTake(attachCallback, nullptr, unlockCallback);
    }

    // Finish workers and finalize on shutdown
    void shutdown(
        const AttachTakeCallback& attachCallback,
        const AttachMidiTakeCallback& attachMidiCallback,
        const UnlockCallback& unlockCallback);

    void shutdown(
        const AttachTakeCallback& attachCallback,
        const UnlockCallback& unlockCallback)
    {
        shutdown(attachCallback, nullptr, unlockCallback);
    }

private:
    static std::string generateTakeFilename(const std::string& folder, int trackId);

    std::unique_ptr<RecordingGroup> m_group;
    std::map<int, std::unique_ptr<TakeRecorder>> m_takes;
    std::map<int, std::unique_ptr<MidiTakeRecorder>> m_midiTakes;
    std::unordered_set<int> m_attachedTakes;
    std::unordered_set<int> m_attachedMidiTakes;
    TempoMap m_tempo;
    int m_recordTrack = -1;
    double m_recordStartBeat = 0.0;
    std::int64_t m_compensation = 0;
    bool m_countInMessagePending = false;
};
