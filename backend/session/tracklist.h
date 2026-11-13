#pragma once
#include <functional>

#include <optional>

#include "session/project.h"
#include "core/audiodata.h"

#include <mutex>
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
        bool reversed = false;
        bool polarityInverted = false;
        EffectState fx;   // clip effects (restMap R2)
        // Fades as played, in beats (restMap G8): the clip's own plus the automatic crossfade
        // where audio clips overlap. Audio clips only; 0 for pattern clips.
        double fadeInBeats = 0.0, fadeOutBeats = 0.0;
        int takeCount = 1, activeTake = 0;   // restMap G11
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
        bool reversed = false;
        bool polarityInverted = false;
        EffectState fx;   // clip effects (restMap R2)
        ClipTakes takes;  // the other takes (restMap G11)
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
    // The audio a clip plays: its file, or for a reversed clip a reversed copy of it (made
    // once, kept in memory, never written). Any thread.
    std::shared_ptr<const AudioData> clipAudio(const AudioClip& clip) const;
    // The pool id the engine asks for when a clip is reversed (audioFor understands it).
    static std::string reversedPoolId(const std::string& poolId);
    // Clip tools (restMap G14) on the given clips, one undo step. Audio clips only: pattern
    // clips among them are left alone. Reverse and InvertPolarity flip their flag (again: back);
    // Normalise sets the clip gain so its loudest sample reaches 0 dBFS (like Reaper).
    enum class ClipTool { Reverse, InvertPolarity, Normalise };
    // Effects on a time range (restMap R2): the audio clip on the track that the range covers
    // most is split at the range's edges (where they fall inside it), as one undo step; the id
    // of the piece inside the range, whose effects id is Project::clipEffectsId(id). -1 when no
    // audio clip is under the range, while recording, or for a bad range.
    int splitForRangeEffects(int trackId, double startBeat, double endBeat);
    // Import (restMap E2): each decoded file (its path set to where it is kept) as a clip at
    // startBeat. targetTrackId < 0: a new Audio track per file, named after it; otherwise all on
    // that track. One undo step either way. The ids of the clips added (empty: refused).
    std::vector<int> importAudio(const std::vector<std::shared_ptr<const AudioData>>& files, double startBeat,
                                 int targetTrackId = -1, std::vector<int>* newTrackIds = nullptr);
    // Whether a clip can have clip effects: an audio clip (a pattern clip's sound is the
    // track's instrument), not while recording.
    bool canHaveClipEffects(int clipId) const;
    bool canApplyClipTool(const std::vector<int>& ids) const;
    Change applyClipTool(const std::vector<int>& ids, ClipTool tool);
    // Takes (restMap G11), each one undo step over the given clips; clips with one take are left
    // alone. setActiveTake plays take `index` (where that take reaches the clip's start);
    // nextTake moves each clip on to its next take (after the last: the first); deleteActiveTake
    // removes the playing take (the next one plays); cropToActiveTake keeps only the playing
    // one. Refused while recording; {true, false} when nothing changed.
    Change setActiveTake(const std::vector<int>& ids, int index);
    Change nextTake(const std::vector<int>& ids);
    Change deleteActiveTake(const std::vector<int>& ids);
    Change cropToActiveTake(const std::vector<int>& ids);
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
    Change clearTrackColour(int id);   // a new track has no colour; this goes back to that
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
    // Mixing plugins (restMap G7): a track's settings for one type (defaults when never set);
    // nullopt for a bad track or type. Not undoable, like the EQs.
    std::optional<mixfx::Settings> mixEffect(int id, std::string_view type) const;
    Change setMixEffect(int id, std::string_view type, const mixfx::Settings& settings);
    // Noise reduction (restMap G13). ownerAudio: an effects owner's recorded audio (a track's
    // audio clips, or the one clip for a clip-effects id) over [from, to) project samples,
    // mixed to mono at the project's rate with clip gain, before any effects; empty when no
    // audio plays there. learnNoise: the noise reduction on that owner learns its noise from
    // [startBeat, endBeat). Empty on success, otherwise why not (for the page). Not undoable,
    // like other effect settings.
    std::vector<float> ownerAudio(int effectsId, std::int64_t from, std::int64_t to) const;
    std::string learnNoise(int effectsId, double startBeat, double endBeat);
    Change setHarmonium(int id, const harmonium::Settings& settings);
    Change setDrumKit(int id, const drumkit::Settings& settings);
    Change setElectroDrums(int id, const electrodrums::Settings& settings);
    Change setPiano(int id, const piano::Settings& settings);
    Change setChords(int id, const chords::Settings& settings);
    Change setSynth(int id, const synth::Settings& settings);
    Change setBass(int id, const bass::Settings& settings);
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
    // A new pattern clip that already holds `notes` (clip-relative beats), as one undo step
    // (drum grooves). -1 when the clip or a note is not valid, or edits are locked.
    int addPatternClipAtBeat(int trackId, double start, double length, std::vector<MidiNote> notes);
    bool trimClipStartAtBeat(int trackId, int clipId, double start);
    int splitClipAtBeat(int trackId, int clipId, double beat);
    Change editClips(const std::vector<int>& ids, const std::string& operation, double delta = 0, int lanes = 0);
    bool canMergeClips(int trackId, int first, int second) const;
    bool canMergeSelectedClips(const std::vector<int>& ids) const;
    bool mergeSelectedClips(const std::vector<int>& ids);
    bool mergeClips(int trackId, int first, int second);
    bool moveClipAtBeat(int trackId, int clipId, double start);
    // Lines a clip's music up with the project's bars: `barLine` is where a bar of the music
    // starts now, in project samples (it may lie before the clip, or before 0). The clip slides
    // by less than a bar, to the nearest project bar line, or a bar later when that would put
    // it before 0. One undo step ("Move clip"). The samples it moved by (0: it was lined up
    // already); nothing for a bad clip or while recording.
    std::optional<std::int64_t> alignClipToBars(int trackId, int clipId, std::int64_t barLine);
    bool resizeClipAtBeat(int trackId, int clipId, double length);
    // continueEdit: part of an ongoing fader drag; see Project::setClipGain.
    Change setClipGain(int trackId, int clipId, double gainDb, bool continueEdit = false);
    // A clip's own fade-in (fadeOut false) or fade-out, in beats from that edge (restMap G8).
    // Audio clips only; undoable, a drag (continueEdit) is one step; it reaches at most the other fade.
    Change setClipFade(int trackId, int clipId, bool fadeOut, double beats, bool continueEdit = false);
    // Pattern notes in clip-relative beats (0 = clip start). Only pattern (non-recorded) clips
    // hold notes. Notes outside the clip bounds are kept (hidden), so trims can be undone.
    std::vector<MidiNote> clipNotes(int trackId, int clipId) const;
    // Project beat at which the clip starts (the origin of clipNotes()); 0 if the clip is missing.
    double clipStartBeat(int trackId, int clipId) const;
    // The clip's length in beats (as the timeline shows it); 0 if the clip is missing.
    double clipLengthBeats(int trackId, int clipId) const;
    Change setClipNotes(int trackId, int clipId, std::vector<MidiNote> notes);
    bool isPatternClip(const AudioClip& clip) const;
    // Volume envelope (restMap G6), positions in project samples. The undoable edits are refused
    // while recording (like clip edits); show / hide and over clips are view state and always work.
    int addEnvelopePoint(int trackId, std::int64_t position, double level);   // index, or -1
    Change moveEnvelopePoint(int trackId, int index, std::int64_t position, double level, bool continueEdit = false);
    Change removeEnvelopePoint(int trackId, int index);
    Change setEnvelopePointShape(int trackId, int index, automation::CurveShape shape);
    Change setEnvelopeBypassed(int trackId, bool bypassed);
    Change setEnvelopeShown(int trackId, bool shown);
    Change setEnvelopeOverClips(int trackId, bool overClips);
    // The same edits in project beats (what the timeline draws); the points stay in samples.
    // A nonfinite beat is refused; so is a negative one when adding (a move stops at 0).
    int addEnvelopePointAtBeat(int trackId, double beat, double level);
    Change moveEnvelopePointAtBeat(int trackId, int index, double beat, double level, bool continueEdit = false);
    struct EnvelopePointInfo { double beat; double level; automation::CurveShape shape; };
    std::vector<EnvelopePointInfo> envelopePointBeats(int trackId) const;   // empty for a bad id
    // The envelope's level at a beat (what the engine plays there; unity with no points).
    double envelopeLevelAtBeat(int trackId, double beat) const;
    // The line to draw from fromBeat to toBeat: `steps` + 1 evenly spaced (beat, level) samples,
    // plus every point in the range and the sample just before it, so corners and Hold jumps
    // are exact. Sorted by beat; empty for a bad track or range.
    struct EnvelopeCurveSample { double beat; double level; };
    std::vector<EnvelopeCurveSample> envelopeCurve(int trackId, double fromBeat, double toBeat, int steps) const;
    bool removeAudioClip(int trackId, int clipId);
    bool copyClips(const std::vector<int>& ids);
    // ripple (restMap G15, "ripple all tracks" for ranges and paste, the clip's own track for
    // clip deletes): close the gap a delete leaves, or make room for what is pasted or duplicated.
    std::vector<int> cutClips(const std::vector<int>& ids, bool ripple = false);
    Change deleteClips(const std::vector<int>& ids, bool ripple = false);
    std::vector<int> pasteClips(int targetTrackId, double targetBeat, bool ripple = false);
    std::vector<int> duplicateClips(const std::vector<int>& ids, double beatOffset = 0.0, bool ripple = false);
    // Time selection edits (restMap G15), on every track, each one undo step. Clips that cross
    // an edge are split there and only the part inside is taken; volume-envelope points inside
    // go too, and a point at each edge keeps the line outside the range where it was. Copy
    // takes the parts inside (the document stays) with the envelope line, the gap before the
    // first clip included. ripple: everything after the range moves left by its length.
    bool copyRange(double startBeat, double endBeat);
    Change deleteRange(double startBeat, double endBeat, bool ripple);
    Change cutRange(double startBeat, double endBeat, bool ripple);
    bool hasClipboard() const { return !m_clipboard.empty() || !m_clipboardEnvelopes.empty(); }
    int clipboardClipCount() const { return static_cast<int>(m_clipboard.size()); }
    const std::vector<ClipboardClip>& clipboard() const { return m_clipboard; }
    // A copied range's envelope lines: points as beats from the range start, per track.
    struct ClipboardEnvelope { int trackOffset = 0; std::vector<std::pair<double, automation::EnvelopePoint>> points; };
    const std::vector<ClipboardEnvelope>& clipboardEnvelopes() const { return m_clipboardEnvelopes; }
    double clipboardLengthBeats() const { return m_clipboardLengthBeats; }
    void clearClipboard() { m_clipboard.clear(); m_clipboardEnvelopes.clear(); m_clipboardLengthBeats = 0.0; }
    bool canUndo() const { return !editsLocked() && m_project.canUndo(); }
    bool canRedo() const { return !editsLocked() && m_project.canRedo(); }
    bool undo();
    bool redo();
    // The next undo/redo adds or removes a whole track (for models that announce row changes).
    std::optional<Project::TrackChange> undoRestoresTrack() const { return canUndo() ? m_project.undoRestoresTrack() : std::nullopt; }
    std::optional<Project::TrackChange> redoRemovesTrack() const { return canRedo() ? m_project.redoRemovesTrack() : std::nullopt; }
    bool setTempo(double bpm, int numerator, int denominator);
    void setSampleRate(double rate);
    // start may be below 0 after latency compensation (restMap G10): the part before 0 is trimmed
    // off the front (the clip starts later in its file).
    //
    // How a recording lands (restMap G11), in project samples. recordStart: where the playhead
    // was when recording began. Loop (loopEnd > loopStart): it ran over the repeat loop; every
    // complete pass becomes a take of one clip spanning the loop, the last one playing (a pass
    // started before the loop counts from the loop start); with no complete pass the half-pass
    // is kept as it was recorded. Punch (punchTo > punchFrom): only what lies inside is kept;
    // when the range sits inside one clip of the same kind, that clip is split there and its
    // old part becomes the first take under the new one, otherwise what was there is replaced.
    // Without either: the clip goes where it was recorded, as before. One undo step.
    struct TakeShape {
        std::int64_t recordStart = 0;
        std::int64_t loopStart = 0, loopEnd = 0;
        std::int64_t punchFrom = 0, punchTo = 0;
        bool loops() const noexcept { return loopEnd > loopStart; }
        bool punches() const noexcept { return punchTo > punchFrom; }
    };
    bool attachTake(int trackId, std::int64_t start, std::shared_ptr<const AudioData> audio, bool printed,
                    const TakeShape& shape);
    bool attachTake(int trackId, std::int64_t start, std::shared_ptr<const AudioData> audio, bool printed)
    { return attachTake(trackId, start, std::move(audio), printed, TakeShape{}); }
    bool attachMidiTake(int trackId, std::int64_t start, std::int64_t length, std::vector<MidiNote> notes,
                        const TakeShape& shape);
    bool attachMidiTake(int trackId, std::int64_t start, std::int64_t length, std::vector<MidiNote> notes)
    { return attachMidiTake(trackId, start, length, std::move(notes), TakeShape{}); }

private:
    // After a track is removed: if it was selected, select the track that took its row.
    void selectNeighbourOfRemoved(int id, int removedRow);
    double offsetBeats(const AudioClip& clip) const;
    // Time selection helpers (restMap G15), in project samples.
    double sourceRatio(const AudioClip& clip) const;   // file frames per project sample
    double takeRatio(const std::string& poolId, bool reversed) const;   // the same for a take
    Change editTakes(const std::vector<int>& ids, const std::function<bool(AudioClip&)>& edit);
    // The part of a clip in [from, to): offset, fades (kept only on the clip's own edges) and
    // pattern notes (those starting in it) follow; the id stays.
    AudioClip clipPiece(const AudioClip& clip, std::int64_t from, std::int64_t to) const;
    std::vector<AudioClip> clipsWithoutSpan(const Track& track, std::int64_t from, std::int64_t to, bool ripple);
    // Where a recording's takes go (restMap G11): the unwrapped start of each kept pass (the
    // last plays), where they play and for how long. Empty passes: nothing to keep.
    struct TakeLayout { std::vector<double> passFrom; double at = 0, length = 0; };
    static TakeLayout takeLayout(double start, double length, const TakeShape& shape);
    // Puts a recorded clip on the track as TakeShape describes for a punch (one undo step).
    bool placeRecorded(int trackId, AudioClip clip, bool punch);
    std::vector<AudioClip> clipsWithSpace(const std::vector<AudioClip>& clips, std::int64_t at, std::int64_t length);
    static automation::VolumeEnvelope envelopeWithoutSpan(const automation::VolumeEnvelope& envelope, std::int64_t from, std::int64_t to, bool ripple);
    static automation::VolumeEnvelope envelopeWithSpace(const automation::VolumeEnvelope& envelope, std::int64_t at, std::int64_t length);
    static automation::VolumeEnvelope envelopeWithPasted(const automation::VolumeEnvelope& envelope, std::int64_t at, std::int64_t length,
                                                         const std::vector<automation::EnvelopePoint>& pasted);
    static bool sameClips(const std::vector<AudioClip>& a, const std::vector<AudioClip>& b);
    Change applyContent(std::vector<Project::TrackContent> contents);
    std::vector<int> pasteWithContent(int targetRow, double targetBeat, bool ripple);
    Project m_project;
    AudioMap m_audio;
    // Reversed copies of audio files (G14), by pool id. Shared, so copying a TrackList keeps
    // one cache; the mutex because the engine's snapshot builder may ask from a worker thread.
    struct ReversedCache {
        std::mutex mutex;
        std::unordered_map<std::string, std::shared_ptr<const AudioData>> copies;
    };
    std::shared_ptr<ReversedCache> m_reversed = std::make_shared<ReversedCache>();
    MediaPaths m_mediaPaths;
    std::unordered_set<int> m_recordingTracks;
    std::optional<bool> m_newTrackRecordDry;
    int startTrack(Track& track); // applies new-track defaults, returns the id
    std::vector<ClipboardClip> m_clipboard;
    std::vector<ClipboardEnvelope> m_clipboardEnvelopes;
    double m_clipboardLengthBeats = 0.0;   // the copied span (a range's length, or the clips')
    int m_selectedTrackId = -1;
    int m_selectedClipId = -1;
    int m_nextAudioNumber = 1, m_nextVocalNumber = 1, m_nextInstrumentNumber = 1, m_nextClipNumber = 1;
    std::unordered_map<std::string, int> m_instrumentNumbers;
};
