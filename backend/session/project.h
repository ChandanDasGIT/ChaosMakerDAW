#pragma once
#include "transport/tempomap.h"

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
};

enum class TrackKind { Audio, Instrument };

struct Track {
    int id = 0;
    TrackKind kind = TrackKind::Audio;
    std::string name;
    std::string type;
    bool solo = false;
    bool muted = false;
    bool recordArmed = false;
    bool recordDry = false;
    std::string inputPort;
    float volume = 1.0f;
    std::array<float, 10> eqGainDb{};
    std::vector<AudioClip> audioClips;

    bool hasLiveInput() const { return kind == TrackKind::Audio || type == "Electric Guitar" || type == "Acoustic Guitar"; }
    bool recordsWithFx() const { return hasLiveInput() && !recordDry; }
};

class Project {
public:
    Track& addTrack(TrackKind kind, const std::string& name, const std::string& type = {});
    void removeTrack(int id);
    void moveTrack(int from, int to);
    Track* findTrack(int id);
    const Track* findTrack(int id) const;
    bool setTrackSolo(int id, bool enabled);
    bool setTrackMuted(int id, bool enabled);
    bool setTrackRecordArmed(int id, bool enabled);
    bool setTrackInputPort(int id, const std::string& port);
    bool setTrackVolume(int id, float volume);
    bool setTrackEqBand(int id, int band, float gainDb);
    int addAudioClip(int trackId, AudioClip clip);
    bool removeAudioClip(int trackId, int clipId);
    bool resizeAudioClip(int trackId, int clipId, std::int64_t lengthSamples);
    bool setClipBounds(int trackId, int clipId, std::int64_t start, std::int64_t length, std::int64_t offset);
    int splitAudioClip(int trackId, int clipId, std::int64_t position, std::int64_t sourceOffset);
    bool moveAudioClip(int trackId, int clipId, std::int64_t start);
    bool canUndo() const { return m_historyPosition > 0; }
    bool canRedo() const { return m_historyPosition < m_history.size(); }
    int undo();
    int redo();
    void clearHistory() { m_history.clear(); m_historyPosition = 0; }

    const std::vector<std::unique_ptr<Track>>& tracks() const { return m_tracks; }
    double contentEndBeats() const noexcept;
    TempoMap& tempo() { return m_tempo; }
    const TempoMap& tempo() const { return m_tempo; }

private:
    struct ClipEdit { int trackId; std::vector<AudioClip> before, after; };
    void rememberClipEdit(int trackId, std::vector<AudioClip> before);
    std::vector<ClipEdit> m_history;
    std::size_t m_historyPosition = 0;
    std::vector<std::unique_ptr<Track>> m_tracks;
    TempoMap m_tempo;
    int m_nextId = 1;
    int m_nextClipId = 1;
};
