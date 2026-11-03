#pragma once

#include "recording/recordinggroup.h"
#include "recording/takerecorder.h"
#include "session/project.h"
#include "engine/enginecommands.h"
#include "transport/tempomap.h"

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

    bool isBusy() const noexcept { return !m_takes.empty(); }
    int primaryRecordingTrackId() const noexcept { return m_recordTrack; }
    std::vector<int> activeRecordingTrackIds() const;
    double recordStartBeat() const noexcept { return m_recordStartBeat; }
    void setRecordStartBeat(double beat) noexcept { m_recordStartBeat = beat; }
    std::size_t takeCount() const noexcept { return m_takes.size(); }

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
        const AttachTakeCallback& attachCallback);

    // Finalizes all takes, attaches any remaining audio, unlocks tracks, and resets group state
    FinalizeResult completeTake(
        const AttachTakeCallback& attachCallback,
        const UnlockCallback& unlockCallback);

    // Finish workers and finalize on shutdown
    void shutdown(
        const AttachTakeCallback& attachCallback,
        const UnlockCallback& unlockCallback);

private:
    static std::string generateTakeFilename(const std::string& folder, int trackId);

    std::unique_ptr<RecordingGroup> m_group;
    std::map<int, std::unique_ptr<TakeRecorder>> m_takes;
    std::unordered_set<int> m_attachedTakes;
    int m_recordTrack = -1;
    double m_recordStartBeat = 0.0;
    bool m_countInMessagePending = false;
};
