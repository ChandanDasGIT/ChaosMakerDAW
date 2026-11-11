#pragma once

#include <optional>
#include <utility>
#include <string_view>
#include "transport/tempomap.h"
#include "midi/midinote.h"
#include "plugins/effectchain.h"
#include "plugins/chaoseq/chaoseqsettings.h"
#include "instruments/drums/drumkitsettings.h"
#include "instruments/electrodrums/electrosettings.h"
#include "instruments/piano/pianosettings.h"
#include "instruments/harmonium/harmoniumsettings.h"
#include "plugins/chordplayer/chordsettings.h"
#include "automation/volumeenvelope.h"
#include "plugins/mixfx/mixfxspec.h"
#include "plugins/effectstate.h"
#include "session/cliptakes.h"

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
    // Clip tools (restMap G14), non-destructive. A reversed clip reads a reversed copy of its
    // audio (TrackList::clipAudio), so sourceOffset counts in that copy and trims / splits work
    // the same way; polarity inverted plays the clip upside down (gain times -1).
    bool reversed = false;
    bool polarityInverted = false;
    // Clip effects (restMap R2): run on this clip's audio before the track's effects. Audio
    // clips only. Copied with the clip (paste, duplicate, split: each piece its own copy).
    EffectState fx;
    // Pattern notes. startBeat is measured from the clip's content origin
    // (start - sourceOffset), so trims/splits keep notes fixed in time like audio.
    std::vector<MidiNote> notes;
    // The other takes (restMap G11; session/cliptakes.h). The fields above are the active one.
    ClipTakes takes;
};

// Clip fades as they play (restMap G8). FadeSpan is one clip on a track; playedFades turns a
// track's spans' own fades into the played ones: where two clips partly overlap, the earlier
// fades out and the later fades in across the overlap (at least; a longer own fade stays), so
// the crossfade follows the clips and is never stored. A clip inside another gets none. Each
// clip's two fades together never exceed its length.
struct FadeSpan { std::int64_t start = 0, length = 0, fadeIn = 0, fadeOut = 0; };
void playedFades(std::vector<FadeSpan>& clips);

enum class TrackKind { Audio, Instrument };

// A track's effect data (chain, EQ, Chaos EQ, mixing plugins) is its EffectState part.
struct Track : EffectState {
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
    // Harmonium tracks: stops, bellows, drones... (the harmonium instrument).
    harmonium::Settings harmonium;
    // Drums tracks: room, part levels, tuning, character (the acoustic drum kit).
    drumkit::Settings drumkit;
    // Electronic Drums tracks: style, each drum's tune / decay / level, volume.
    electrodrums::Settings electrodrums;
    // Piano tracks: volume, release, resonance, velocity, brightness, scale changer.
    piano::Settings piano;
    // Chords tracks: the eight strips' chords, autoplay and key (its piano uses `piano` above).
    chords::Settings chords;
    std::vector<AudioClip> audioClips;
    // Volume envelope (restMap G6): multiplies the fader; no points = no effect.
    automation::VolumeEnvelope volumeEnvelope;

    bool hasLiveInput() const;
    // MON is offered for a live audio input (guitar, vocal) and for every instrument track,
    // whose live input is MIDI (a keyboard plays it).
    bool canMonitor() const { return hasLiveInput() || kind == TrackKind::Instrument; }
    bool recordsWithFx() const { return hasLiveInput() && !recordDry; }
    // Semitones the track's instrument moves every note it is sent (a scale changer): a played
    // C4 sounds C4 + keyShift(). The harmonium's and the piano's scale changers; 0 elsewhere.
    int keyShift() const;
};

class Project {
public:
    // The master track (restMap G9): its insert effects run on the whole mix, before the master
    // fader. It has this fixed id; findTrack() finds it, so the effect calls (add, remove, move,
    // EQ, Chaos EQ, mixing plugins) work on it, but it is never in tracks(): the track list,
    // timeline, mixer lanes, solo / mute and recording never see it.
    static constexpr int MasterTrackId = 2147483000;
    // Clip effects (restMap R2): an audio clip's effects have this id (base + clip id), so the
    // same effect calls, plugin pages and FX window work on them.
    static constexpr int ClipEffectsBase = 1000000000;
    static constexpr int clipEffectsId(int clipId) noexcept { return ClipEffectsBase + clipId; }
    static constexpr bool isClipEffectsId(int id) noexcept { return id > ClipEffectsBase && id < MasterTrackId; }
    // The effect data behind an id: a track's, the master's or a clip's; null for none.
    EffectState* effects(int id);
    const EffectState* effects(int id) const;
    // The owner's type, for the chain rules: a track's type, "Master", or "Clip".
    std::string effectOwnerType(int id) const;
    // The clip whose effects an id names, and its track; null for none.
    const AudioClip* clipOfEffects(int id, int* trackId = nullptr) const;
    Project();
    Track& master() noexcept { return m_master; }
    const Track& master() const noexcept { return m_master; }
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
    // Not undoable, like the 10-band EQ. The settings are sanitised for the project rate.
    bool setTrackChaosEq(int id, const chaoseq::Settings& settings);
    // False for a bad track or a type that is not a mixing plugin; values are sanitized.
    bool setTrackMixEffect(int id, std::string_view type, const mixfx::Settings& settings);
    // Not undoable either, like the instrument knobs of other DAWs. The settings are sanitised.
    bool setTrackHarmonium(int id, const harmonium::Settings& settings);
    bool setTrackDrumKit(int id, const drumkit::Settings& settings);   // the same way
    bool setTrackElectroDrums(int id, const electrodrums::Settings& settings);   // and again
    bool setTrackPiano(int id, const piano::Settings& settings);
    bool setTrackChords(int id, const chords::Settings& settings);
    // Insert effects. Not undoable by design (the user removes an effect by hand); undoing a
    // track removal still brings the track back with its effects.
    int addTrackEffect(int trackId, const std::string& type); // new effect id, or -1
    bool removeTrackEffect(int trackId, int effectId);
    bool moveTrackEffect(int trackId, int effectId, int toIndex);
    // Replace the whole list (loading a file): types in order, each known, at most once, and
    // GuitarSuite only where the track type has it. A missing fixed effect (GuitarSuite on a
    // guitar) is put back at the front. False, and no change, if the list breaks a rule.
    bool setTrackEffects(int trackId, const std::vector<std::string>& types);
    int addAudioClip(int trackId, AudioClip clip);
    std::vector<int> addAudioClips(const std::vector<std::pair<int, AudioClip>>& clips);
    bool removeAudioClip(int trackId, int clipId);
    bool resizeAudioClip(int trackId, int clipId, std::int64_t lengthSamples);
    bool setClipBounds(int trackId, int clipId, std::int64_t start, std::int64_t length, std::int64_t offset);
    int splitAudioClip(int trackId, int clipId, std::int64_t position, std::int64_t sourceOffset);
    // Joins the last two undo steps into one when both are clip edits of the same one track
    // (two splits made by one command). False, and nothing changes, otherwise.
    bool joinLastClipEdits();
    bool canMergeAudioClips(int trackId, int first, int second, double sourceRate) const;
    bool mergeAudioClips(int trackId, int first, int second, double sourceRate);
    bool moveAudioClip(int trackId, int clipId, std::int64_t start);
    // continueEdit: fold this change into the previous undo step when that step was a gain
    // edit of the same clip (one fader drag = one undo step). Otherwise a new step is added.
    bool setClipGain(int trackId, int clipId, float gainDb, bool continueEdit = false);
    // A clip's own fades (restMap G8), in samples, each kept within the clip. continueEdit: part
    // of an ongoing handle drag on the same clip, so the whole drag is one undo step.
    bool setClipFades(int trackId, int clipId, std::int64_t fadeIn, std::int64_t fadeOut, bool continueEdit = false);
    // Replace a clip's pattern notes (origin-relative beats) as one undoable edit.
    bool setClipNotes(int trackId, int clipId, std::vector<MidiNote> notes);
    // Volume envelope (restMap G6). Adding, moving and removing points and bypass are undoable;
    // showing the lane and drawing it over the clips are view state and are not. Showing an
    // envelope with no points adds a start point (sample 0, 0 dB) with no undo step; loading a
    // project does not.
    // addEnvelopePoint returns the new point's index, or -1 (see VolumeEnvelope::addPoint).
    // continueEdit: fold the move into the previous undo step when that step moved the same point
    // of the same track (one drag = one undo step).
    int addEnvelopePoint(int trackId, std::int64_t position, double level);
    bool moveEnvelopePoint(int trackId, int index, std::int64_t position, double level, bool continueEdit = false);
    bool removeEnvelopePoint(int trackId, int index);
    // The curve from a point to the next (restMap G6b); undoable. False for a bad track / index.
    bool setEnvelopePointShape(int trackId, int index, automation::CurveShape shape);
    bool setEnvelopeBypassed(int trackId, bool bypassed);
    bool setEnvelopeShown(int trackId, bool shown);
    bool setEnvelopeOverClips(int trackId, bool overClips);
    struct ClipChange { int trackId; AudioClip clip; bool remove = false; int destinationTrackId = -1; };
    bool editClipGroup(const std::vector<ClipChange>& changes);
    // Time selection edits (restMap G15): several tracks' clips and volume-envelope points
    // (the points and bypass; the view state stays) replaced as one undo step. A new clip
    // takes its id from takeClipId(). False, and no change, for an unknown or repeated track,
    // a repeated clip id or a clip with bad bounds.
    struct TrackContent { int trackId; std::vector<AudioClip> clips; automation::VolumeEnvelope envelope; };
    bool replaceTrackContent(std::vector<TrackContent> contents);
    int takeClipId() noexcept { return m_nextClipId++; }
    bool canUndo() const { return m_historyPosition > 0; }
    bool canRedo() const { return m_historyPosition < m_history.size(); }
    int undo();
    int redo();
    // When the next undo/redo adds or removes a whole track: which track and at what row.
    struct TrackChange { int trackId; std::size_t index; };
    std::optional<TrackChange> undoRestoresTrack() const;
    std::optional<TrackChange> redoRemovesTrack() const;
    void clearHistory() { m_history.clear(); m_historyPosition = 0; }
    // The undo history's step names, oldest first (restMap G16). historyMark() of them are
    // done; the ones after it are undone. At most 100 steps are kept.
    std::vector<std::string> historyLabels() const;
    // How many steps were ever added: a counter that only grows (the history is capped, so its
    // size cannot tell). A caller compares it before and after to see whether a step was added.
    std::uint64_t stepCount() const noexcept { return m_stepCount; }
    std::size_t historySize() const noexcept { return m_history.size(); }
    // Renames the newest step while it is the current one, for an action made of several calls
    // (e.g. "Paste"). The label is kept as a pointer: pass a string literal. False otherwise.
    bool setLastStepLabel(const char* label) noexcept;
    // Several steps since `mark` (a history size taken before) become one step that added the
    // tracks `ids` (an import, restMap E2): undo removes them with their clips, redo puts them
    // back. False, and nothing changes, for no ids or a mark past the history.
    // The current place in the history (a new step drops the undone ones after it).
    std::size_t historyMark() const noexcept { return m_historyPosition; }
    bool foldIntoAddedTracks(std::size_t mark, const std::vector<int>& ids);
    // Whether the next undo / redo adds or removes several tracks at once (an import): views
    // then reload the whole list.
    bool undoChangesTracks() const;
    bool redoChangesTracks() const;

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
    // movedPoint: set when the step moved one envelope point, so a drag can extend it.
    struct EnvelopeEdit { int trackId; automation::VolumeEnvelope before, after; int movedPoint = -1; };
    struct ClipEdit {
        std::vector<TrackClipEdit> tracks;
        std::optional<EnvelopeEdit> envelope;
        std::vector<EnvelopeEdit> envelopes;   // several tracks' points with their clips (G15)
        int gainClipId = -1;
        int fadeClipId = -1;      // a fade handle drag (G8), extended like gainClipId
        std::optional<Track> removedTrack;
        std::size_t removedIndex = 0;
        // Tracks this step added (an import): their ids, and while undone their copies at
        // their rows so redo can put them back.
        std::vector<int> addedTrackIds;
        std::vector<std::pair<std::size_t, Track>> addedTracks;
        const char* label = "Edit";   // the step's name in the undo history (G16)
    };
    void pushHistory(ClipEdit edit);
    std::size_t indexOfTrack(int id) const;
    void rememberClipEdit(int trackId, std::vector<AudioClip> before, const char* label);
    void restoreClips(int trackId, std::vector<AudioClip> clips);
    void rememberEnvelopeEdit(int trackId, automation::VolumeEnvelope before, const char* label, int movedPoint = -1);
    std::vector<ClipEdit> m_history;
    std::size_t m_historyPosition = 0;
    std::uint64_t m_stepCount = 0;
    std::vector<std::unique_ptr<Track>> m_tracks;
    Track m_master;
    TempoMap m_tempo;
    float m_masterVolume = 1.0f;
    bool m_masterMuted = false;
    int m_nextId = 1;
    int m_nextEffectId = 1;
    int m_nextClipId = 1;
};
