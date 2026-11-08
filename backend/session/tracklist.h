#pragma once

#include <optional>

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
        double gainDb = 0.0;
        bool isRecorded = false;
        std::vector<MidiNote> notes; // clip-relative beats (0 = clip start); may lie outside the clip
    };
    struct ClipboardClip {
        std::string poolId;
        double beatOffset = 0.0;
        double lengthBeats = 0.0;
        std::int64_t sourceOffset = 0;
        std::int64_t fadeIn = 0;
        std::int64_t fadeOut = 0;
        float gainDb = 0.0f;
        bool effectsPrinted = true;
        int trackOffset = 0;
        std::vector<MidiNote> notes; // origin-relative, as stored in AudioClip
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
    // The instruments in catalog order, a group (Guitar, Drums) once, where its first kind is:
    // a group's id is GroupPrefix + its name and it has children (variantChoices).
    static std::vector<TrackChoice> instrumentChoices();
    // The kinds in one group ("Guitar": Acoustic, Electric), ids are the track types to add.
    // Accepts the group's name or its choice id; empty for anything else.
    static std::vector<TrackChoice> variantChoices(const std::string& group);
    static constexpr std::string_view GroupPrefix = "group:";
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
    int selectedTrackId() const { return m_selectedTrackId; }
    bool selectTrack(int id);
    int selectedClipId() const { return m_selectedClipId; }
    bool selectClip(int trackId, int clipId);
    void clearSelectedClip() { m_selectedClipId = -1; }
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
    Change setPan(int id, double pan);
    Change setStripWidth(int id, double width);
    Change setEqBand(int id, int band, double gainDb);
    Change setChaosEq(int id, const chaoseq::Settings& settings);
    Change setHarmonium(int id, const harmonium::Settings& settings);
    Change setDrumKit(int id, const drumkit::Settings& settings);
    Change setElectroDrums(int id, const electrodrums::Settings& settings);
    Change setPiano(int id, const piano::Settings& settings);
    // Recording mode for tracks the user adds from now on (the "default recording mode"
    // preference); unset keeps Project's rule (dry only for plain Audio tracks). Loaded tracks
    // keep their saved mode.
    void setNewTrackRecordDry(std::optional<bool> dry) { m_newTrackRecordDry = dry; }
    // Insert effects (plan 6.1); refused on a track that is recording. Not undoable.
    int addTrackEffect(int trackId, const std::string& type); // new effect id, or -1
    Change removeTrackEffect(int trackId, int effectId);
    Change moveTrackEffect(int trackId, int effectId, int toIndex);
    double masterVolume() const noexcept { return m_project.masterVolume(); }
    Change setMasterVolume(double volume);
    bool masterMuted() const noexcept { return m_project.masterMuted(); }
    Change setMasterMuted(bool muted);
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
    // continueEdit: part of an ongoing fader drag; see Project::setClipGain.
    Change setClipGain(int trackId, int clipId, double gainDb, bool continueEdit = false);
    // Pattern notes in clip-relative beats (0 = clip start). Only pattern (non-recorded) clips
    // hold notes. Notes outside the clip bounds are kept (hidden), so trims can be undone.
    std::vector<MidiNote> clipNotes(int trackId, int clipId) const;
    // Project beat at which the clip starts (the origin of clipNotes()); 0 if the clip is missing.
    double clipStartBeat(int trackId, int clipId) const;
    // The clip's length in beats (as the timeline shows it); 0 if the clip is missing.
    double clipLengthBeats(int trackId, int clipId) const;
    Change setClipNotes(int trackId, int clipId, std::vector<MidiNote> notes);
    bool isPatternClip(const AudioClip& clip) const;
    bool removeAudioClip(int trackId, int clipId);
    bool copyClips(const std::vector<int>& ids);
    std::vector<int> cutClips(const std::vector<int>& ids);
    std::vector<int> pasteClips(int targetTrackId, double targetBeat);
    std::vector<int> duplicateClips(const std::vector<int>& ids, double beatOffset = 0.0);
    bool hasClipboard() const { return !m_clipboard.empty(); }
    int clipboardClipCount() const { return static_cast<int>(m_clipboard.size()); }
    const std::vector<ClipboardClip>& clipboard() const { return m_clipboard; }
    void clearClipboard() { m_clipboard.clear(); }
    bool canUndo() const { return !editsLocked() && m_project.canUndo(); }
    bool canRedo() const { return !editsLocked() && m_project.canRedo(); }
    bool undo();
    bool redo();
    // The next undo/redo adds or removes a whole track (for models that announce row changes).
    std::optional<Project::TrackChange> undoRestoresTrack() const { return canUndo() ? m_project.undoRestoresTrack() : std::nullopt; }
    std::optional<Project::TrackChange> redoRemovesTrack() const { return canRedo() ? m_project.redoRemovesTrack() : std::nullopt; }
    bool setTempo(double bpm, int numerator, int denominator);
    void setSampleRate(double rate);
    bool attachTake(int trackId, std::int64_t start, std::shared_ptr<const AudioData> audio, bool printed);
    bool attachMidiTake(int trackId, std::int64_t start, std::int64_t length, std::vector<MidiNote> notes);

private:
    // After a track is removed: if it was selected, select the track that took its row.
    void selectNeighbourOfRemoved(int id, int removedRow);
    double offsetBeats(const AudioClip& clip) const;
    Project m_project;
    AudioMap m_audio;
    MediaPaths m_mediaPaths;
    std::unordered_set<int> m_recordingTracks;
    std::optional<bool> m_newTrackRecordDry;
    int startTrack(Track& track); // applies new-track defaults, returns the id
    std::vector<ClipboardClip> m_clipboard;
    int m_selectedTrackId = -1;
    int m_selectedClipId = -1;
    int m_nextAudioNumber = 1, m_nextVocalNumber = 1, m_nextInstrumentNumber = 1, m_nextClipNumber = 1;
    std::unordered_map<std::string, int> m_instrumentNumbers;
};
