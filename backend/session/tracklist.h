#pragma once

#include "session/project.h"
#include "core/audiodata.h"

#include <unordered_map>
#include <unordered_set>
#include <span>

// UI-thread domain owner. Qt models adapt this state; Project owns document/history mutations.
class TrackList {
public:
    using AudioMap = std::unordered_map<std::string, std::shared_ptr<const AudioData>>;
    using MediaPaths = std::unordered_set<std::string>;
    struct Change {
        bool accepted = false;
        bool changed = false;
        explicit operator bool() const { return accepted; }
    };
    struct ClipInfo {
        int clipId, trackId, laneIndex;
        double startBeat, lengthBeats;
        std::string label, colour;
        bool effectsPrinted;
    };
    struct MonitorState {
        bool enabled = false;
        std::string input;
        double gain = 1;
        std::array<float, 10> eq{};
    };
    struct TrackChoice { std::string id, name; bool hasChildren; };

    Project& project() { return m_project; }
    const Project& project() const { return m_project; }
    // Transitional storage adapters for session serialization, extracted in batch 6.
    AudioMap& audio() { return m_audio; }
    const AudioMap& audio() const { return m_audio; }
    MediaPaths& mediaPaths() { return m_mediaPaths; }
    const MediaPaths& mediaPaths() const { return m_mediaPaths; }
    std::shared_ptr<const AudioData> audioFor(const std::string& path) const;
    void replaceDocument(Project project, AudioMap audio, MediaPaths paths);
    void adoptDocument(TrackList&& candidate);

    static std::vector<TrackChoice> trackChoices();
    static std::vector<TrackChoice> instrumentChoices();
    static bool isInstrumentType(const std::string& type);
    bool canAddChoice(const std::string& id) const;
    int addTrackChoice(const std::string& id);
    int addAudioTrack();
    int addVocalTrack();
    int addInstrumentTrack();
    int addInstrumentTrackOfType(const std::string& type);
    int count() const { return static_cast<int>(m_project.tracks().size()); }
    int indexOfTrack(int id) const;
    int trackIdAt(int row) const;
    bool canRemoveTrack(int id) const;
    bool removeTrack(int id);
    bool canMoveTrack(int id, int row) const;
    bool moveTrack(int id, int row);
    Change renameTrack(int id, const std::string& name);
    Change setTrackColour(int id, const std::string& colour);
    Change setSolo(int id, bool enabled);
    Change setMuted(int id, bool enabled);
    Change setRecordArmed(int id, bool enabled);
    Change setMonitorEnabled(int id, bool enabled);
    Change setRecordDry(int id, bool dry);
    Change setTrackInputPort(int id, const std::string& port);
    Change setVolume(int id, double volume);
    Change setEqBand(int id, int band, double gainDb);
    MonitorState cleanMonitor() const;
    bool isMonitored(int id) const;
    void lockRecordingTracks(std::unordered_set<int> ids) { m_recordingTracks = std::move(ids); }
    void lockRecordingTrack(int id) { m_recordingTracks.clear(); if (id >= 0) m_recordingTracks.insert(id); }
    bool editsLocked() const { return !m_recordingTracks.empty(); }
    bool trackLocked(int id) const { return m_recordingTracks.contains(id); }
    int recordingTrackId() const;

    std::vector<ClipInfo> clips() const;
    const AudioClip* findClip(int trackId, int clipId) const;
    int addAudioClip(int trackId, const std::string& poolId, std::int64_t start, std::int64_t length,
                     std::int64_t offset = 0, bool effectsPrinted = true);
    int addClipAtBeat(int trackId, double start, double length);
    bool trimClipStartAtBeat(int trackId, int clipId, double start);
    int splitClipAtBeat(int trackId, int clipId, double beat);
    Change editClips(const std::vector<int>& ids, const std::string& operation, double delta = 0, int lanes = 0);
    bool canMergeClips(int trackId, int first, int second) const;
    bool canMergeSelectedClips(const std::vector<int>& ids) const;
    bool mergeSelectedClips(const std::vector<int>& ids);
    bool mergeClips(int trackId, int first, int second);
    bool moveClipAtBeat(int trackId, int clipId, double start);
    bool resizeClipAtBeat(int trackId, int clipId, double length);
    bool removeAudioClip(int trackId, int clipId);
    bool canUndo() const { return !editsLocked() && m_project.canUndo(); }
    bool canRedo() const { return !editsLocked() && m_project.canRedo(); }
    bool undo();
    bool redo();
    bool setTempo(double bpm, int numerator, int denominator);
    void setSampleRate(double rate);
    bool attachTake(int trackId, std::int64_t start, std::shared_ptr<const AudioData> audio, bool printed);

private:
    Project m_project;
    AudioMap m_audio;
    MediaPaths m_mediaPaths;
    std::unordered_set<int> m_recordingTracks;
    int m_nextAudioNumber = 1, m_nextVocalNumber = 1, m_nextInstrumentNumber = 1, m_nextClipNumber = 1;
    std::unordered_map<std::string, int> m_instrumentNumbers;
};
