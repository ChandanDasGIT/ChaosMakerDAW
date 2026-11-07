#pragma once

#include <optional>
#include "transport/tempomap.h"
#include "midi/midinote.h"
#include "plugins/effectchain.h"

#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

// The document. UI thread only. The audio thread never sees these types.
struct AudioClip {
    int id = 0;
    std::string poolId;
    std::int64_t start = 0;
    std::int64_t length = 0;
    std::int64_t sourceOffset = 0;
    std::int64_t fadeIn = 0;
    std::int64_t fadeOut = 0;
    float gainDb = 0.0f;
    bool effectsPrinted = true;
    // Pattern notes. startBeat is measured from the clip's content origin
    // (start - sourceOffset), so trims/splits keep notes fixed in time like audio.
    std::vector<MidiNote> notes;
};

enum class TrackKind { Audio, Instrument };

struct Track {
    int id = 0;
    TrackKind kind = TrackKind::Audio;
    std::string name;
    std::string type;
    std::string colour = "#388b95";
    bool solo = false;
    bool muted = false;
    bool recordArmed = false;
    bool monitorEnabled = false;
    bool recordDry = false;
    std::string inputPort;
    float volume = 1.0f;
    float pan = 0.0f; // -1 left .. 0 centre .. +1 right (panlaw::balance)
    float stripWidth = 108.0f; // mixer strip width in px (mixer::defaultStripWidth, clamped by mixer::clampStripWidth)
    std::array<float, 10> eqGainDb{};
    // Insert effects in processing order (plan 6.1); rules in effectchain.
    std::vector<TrackEffectSlot> effects;
    std::vector<AudioClip> audioClips;

    bool hasLiveInput() const;
    bool recordsWithFx() const { return hasLiveInput() && !recordDry; }
};

class Project {
public:
    Track& addTrack(TrackKind kind, const std::string& name, const std::string& type = {});
    // Removing a track is an undoable step: undo puts it back (same id, position and content).
    void removeTrack(int id);
    void moveTrack(int from, int to);
    Track* findTrack(int id);
    const Track* findTrack(int id) const;
    bool setTrackName(int id, const std::string& name);
    bool setTrackColour(int id, const std::string& colour);
    bool setTrackSolo(int id, bool enabled);
    bool setTrackMuted(int id, bool enabled);
    bool setTrackRecordArmed(int id, bool enabled);
    bool setTrackInputPort(int id, const std::string& port);
    bool setTrackVolume(int id, float volume);
    bool setTrackPan(int id, float pan);
    bool setTrackStripWidth(int id, float width);
    bool setTrackEqBand(int id, int band, float gainDb);
    // Insert effects. Not undoable by design (the user removes an effect by hand); undoing a
    // track removal still brings the track back with its effects.
    int addTrackEffect(int trackId, const std::string& type); // new effect id, or -1
    bool removeTrackEffect(int trackId, int effectId);
    bool moveTrackEffect(int trackId, int effectId, int toIndex);
    int addAudioClip(int trackId, AudioClip clip);
    std::vector<int> addAudioClips(const std::vector<std::pair<int, AudioClip>>& clips);
    bool removeAudioClip(int trackId, int clipId);
    bool resizeAudioClip(int trackId, int clipId, std::int64_t lengthSamples);
    bool setClipBounds(int trackId, int clipId, std::int64_t start, std::int64_t length, std::int64_t offset);
    int splitAudioClip(int trackId, int clipId, std::int64_t position, std::int64_t sourceOffset);
    bool canMergeAudioClips(int trackId, int first, int second, double sourceRate) const;
    bool mergeAudioClips(int trackId, int first, int second, double sourceRate);
    bool moveAudioClip(int trackId, int clipId, std::int64_t start);
    // continueEdit: fold this change into the previous undo step when that step was a gain
    // edit of the same clip (one fader drag = one undo step). Otherwise a new step is added.
    bool setClipGain(int trackId, int clipId, float gainDb, bool continueEdit = false);
    // Replace a clip's pattern notes (origin-relative beats) as one undoable edit.
    bool setClipNotes(int trackId, int clipId, std::vector<MidiNote> notes);
    struct ClipChange { int trackId; AudioClip clip; bool remove = false; int destinationTrackId = -1; };
    bool editClipGroup(const std::vector<ClipChange>& changes);
    bool canUndo() const { return m_historyPosition > 0; }
    bool canRedo() const { return m_historyPosition < m_history.size(); }
    int undo();
    int redo();
    // When the next undo/redo adds or removes a whole track: which track and at what row.
    struct TrackChange { int trackId; std::size_t index; };
    std::optional<TrackChange> undoRestoresTrack() const;
    std::optional<TrackChange> redoRemovesTrack() const;
    void clearHistory() { m_history.clear(); m_historyPosition = 0; }

    const std::vector<std::unique_ptr<Track>>& tracks() const { return m_tracks; }
    double contentEndBeats() const noexcept;
    TempoMap& tempo() { return m_tempo; }
    const TempoMap& tempo() const { return m_tempo; }

    float masterVolume() const noexcept { return m_masterVolume; }
    bool setMasterVolume(float volume) noexcept;
    bool masterMuted() const noexcept { return m_masterMuted; }
    bool setMasterMuted(bool muted) noexcept;

private:
    struct TrackClipEdit { int trackId; std::vector<AudioClip> before, after; };
    // gainClipId: set when the step is a clip gain change, so a fader drag can extend it.
    // removedTrack: set when the step removed a whole track (kept here so undo can restore it).
    struct ClipEdit {
        std::vector<TrackClipEdit> tracks;
        int gainClipId = -1;
        std::optional<Track> removedTrack;
        std::size_t removedIndex = 0;
    };
    void pushHistory(ClipEdit edit);
    std::size_t indexOfTrack(int id) const;
    void rememberClipEdit(int trackId, std::vector<AudioClip> before);
    std::vector<ClipEdit> m_history;
    std::size_t m_historyPosition = 0;
    std::vector<std::unique_ptr<Track>> m_tracks;
    TempoMap m_tempo;
    float m_masterVolume = 1.0f;
    bool m_masterMuted = false;
    int m_nextId = 1;
    int m_nextEffectId = 1;
    int m_nextClipId = 1;
};
