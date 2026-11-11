#pragma once

#include "session/tracklist.h"

#include <QAbstractListModel>
#include <QVariantList>
#include <QUrl>
#include <QStringList>
#include <QJsonObject>
#include <QSet>
#include "engine/enginesnapshot.h"
#include "timeline/tracklayout.h"
#include <QtQml/qqmlregistration.h>
#include <functional>

class TrackListWrapper : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList trackChoices READ trackChoices CONSTANT)
    Q_PROPERTY(QVariantList instrumentChoices READ instrumentChoices CONSTANT)
    Q_PROPERTY(QStringList eqPresetNames READ eqPresetNames CONSTANT)
    Q_PROPERTY(QString eqPresetError READ eqPresetError NOTIFY eqPresetErrorChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectedTrackId READ selectedTrackId WRITE selectTrack NOTIFY selectedTrackChanged)
    // Semitones the selected track's instrument moves its notes (Track::keyShift: the harmonium's
    // scale changer); the floating piano shows what sounds. 0 with no track selected.
    Q_PROPERTY(int selectedKeyShift READ selectedKeyShift NOTIFY selectedKeyShiftChanged)
    Q_PROPERTY(int selectedClipId READ selectedClipId WRITE setSelectedClipId NOTIFY selectedClipChanged)
    Q_PROPERTY(double contentEndBeats READ contentEndBeats NOTIFY contentEndBeatsChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
    Q_PROPERTY(bool hasClipboard READ hasClipboard NOTIFY clipboardChanged)
    Q_PROPERTY(int clipboardClipCount READ clipboardClipCount NOTIFY clipboardChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    // The undo history (restMap G16): step names oldest first, and how many of them are done
    // (the ones from undoPosition on are undone).
    Q_PROPERTY(QStringList undoHistory READ undoHistory NOTIFY undoHistoryChanged)
    Q_PROPERTY(int undoPosition READ undoPosition NOTIFY undoHistoryChanged)
    Q_PROPERTY(bool cleanMonitorArmed READ cleanMonitorArmed NOTIFY cleanMonitorChanged)
    Q_PROPERTY(QString cleanMonitorInput READ cleanMonitorInput NOTIFY cleanMonitorInputChanged)
    Q_PROPERTY(double cleanMonitorGain READ cleanMonitorGain NOTIFY cleanMonitorGainChanged)
    Q_PROPERTY(QVariantList cleanMonitorEqBands READ cleanMonitorEqBands NOTIFY cleanMonitorEqChanged)
    // Recording mode for tracks added from now on, bound to the "default recording mode"
    // preference (plan 6.6). Not set: dry only for plain Audio tracks.
    Q_PROPERTY(bool newTrackRecordDry READ newTrackRecordDry WRITE setNewTrackRecordDry NOTIFY newTrackRecordDryChanged)
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY masterVolumeChanged)
    Q_PROPERTY(bool masterMuted READ masterMuted WRITE setMasterMuted NOTIFY masterMutedChanged)
    // The master track (restMap G9): its id works with the effect calls (addTrackEffect,
    // setEqBand, the mixing plugins...); its effects and EQ bands in the same form as the
    // "effects" and "eqBands" roles.
    Q_PROPERTY(int masterTrackId READ masterTrackId CONSTANT)
    Q_PROPERTY(QVariantList masterEffects READ masterEffects NOTIFY masterChanged)
    Q_PROPERTY(QVariantList masterEqBands READ masterEqBands NOTIFY masterChanged)
    // Row layout (restMap G6): a track's part, plus its volume lane when shown in its own lane.
    // Heights are set by the views; rowTops has count + 1 values (the last is the total height).
    Q_PROPERTY(double trackRowHeight READ trackRowHeight WRITE setTrackRowHeight NOTIFY layoutChanged)
    Q_PROPERTY(double envelopeLaneHeight READ envelopeLaneHeight WRITE setEnvelopeLaneHeight NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList rowTops READ rowTops NOTIFY layoutChanged)
    Q_PROPERTY(double layoutHeight READ layoutHeight NOTIFY layoutChanged)
    // The envelope's level (0..1) of 0 dB, where a new envelope's line sits.
    Q_PROPERTY(double envelopeUnityLevel READ envelopeUnityLevel CONSTANT)
    // The curve shapes a point can have, in menu order: [{id, name}] (restMap G6b).
    Q_PROPERTY(QVariantList envelopeShapes READ envelopeShapes CONSTANT)

public:
    enum Role {
        TrackIdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        TypeRole,
        SoloRole,
        MutedRole,
        RecordArmedRole,
        MonitorEnabledRole,
        InputPortRole,
        VolumeRole,
        ClipsRole,
        EqBandsRole,
        LiveInputRole,
        CanMonitorRole,
        RecordDryRole,
        RecordsWithFxRole,
        ColourRole,
        PanRole,
        StripWidthRole,
        EffectsRole,  // insert effects in chain order: [{id, type, title, subtitle, removable}] (plan 6.1)
        // Volume envelope (restMap G6). Points: [{beat, level, shape}] in time order, level
        // 0..1, shape an id from envelopeShapes (the curve to the next point).
        EnvelopeShownRole,
        EnvelopeOverClipsRole,
        EnvelopeBypassedRole,
        EnvelopePointsRole
    };
    Q_ENUM(Role)

    explicit TrackListWrapper(QObject* parent = nullptr);
    TrackList& domain() { return m_state; }
    const TrackList& domain() const { return m_state; }
    QVariantList trackChoices() const;
    QVariantList instrumentChoices() const;
    // The kinds inside an instrument group choice (hasChildren), e.g. Drums: Acoustic, Electronic.
    Q_INVOKABLE QVariantList variantChoices(const QString& group) const;
    Q_INVOKABLE int addTrackChoice(const QString& id);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int count() const { return rowCount(); }
    int selectedTrackId() const { return m_state.selectedTrackId(); }
    Q_INVOKABLE bool selectTrack(int id);
    int selectedKeyShift() const;
    int selectedClipId() const { return m_state.selectedClipId(); }
    Q_INVOKABLE void setSelectedClipId(int id);
    Q_INVOKABLE void selectClip(int trackId, int clipId);
    // continueEdit: true for every step of a fader drag after the first (one undo step per drag).
    Q_INVOKABLE bool setClipGain(int trackId, int clipId, double gainDb, bool continueEdit = false);
    Q_INVOKABLE double clipGain(int trackId, int clipId) const;
    // restMap G8: an audio clip's own fade-in (fadeOut false) or fade-out, in beats from that
    // edge; undoable. Clips report their fades as played in "fadeInBeats" / "fadeOutBeats".
    Q_INVOKABLE bool setClipFade(int trackId, int clipId, bool fadeOut, double beats, bool continueEdit = false);
    // Pattern notes of a clip in clip-relative beats: [{id, note, startBeat, lengthBeats, velocity}]
    Q_INVOKABLE QVariantList clipNotes(int trackId, int clipId) const;
    // C++ adapter used by PianoRollWrapper; one undoable edit, emits clip-edit signals on change.
    bool setClipNotes(int trackId, int clipId, std::vector<MidiNote> notes);
    // A new pattern clip holding `notes` (clip-relative beats), as one undo step; -1 if refused.
    int addPatternClipAtBeat(int trackId, double startBeat, double lengthBeats, std::vector<MidiNote> notes);
    double contentEndBeats() const { return m_project.contentEndBeats(); }
    QVariantList clips() const;
    Q_INVOKABLE QVariantList clipWaveform(int trackId, int clipId, int bins, double from = 0, double to = 1) const;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    QJsonObject sessionState() const;
    bool restoreSessionState(const QJsonObject& state, QString& error);
    void replaceDocument(Project project, TrackList::AudioMap audio, TrackList::MediaPaths paths);
    const TrackList::MediaPaths& mediaPaths() const { return m_state.mediaPaths(); }
    std::shared_ptr<const AudioData> audioFor(const std::string& path) const { return m_state.audioFor(path); }
    void adoptDocument(TrackListWrapper& candidate);
    std::unique_ptr<EngineSnapshot> playbackSnapshot() const;
    bool attachTake(int trackId, std::int64_t start, std::shared_ptr<const AudioData> audio, bool effectsPrinted = true);
    bool attachMidiTake(int trackId, std::int64_t start, std::int64_t length, std::vector<MidiNote> notes);
    int recordingTrackId() const;
    const Project& project() const { return m_project; }
    void setSampleRate(double rate);
    void lockRecordingTrack(int id) { m_state.lockRecordingTrack(id); emit historyChanged(); }
    void lockRecordingTracks(const QSet<int>& ids);
    bool canUndo() const { return m_state.canUndo(); }
    bool canRedo() const { return m_state.canRedo(); }
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    QStringList undoHistory() const;
    int undoPosition() const { return static_cast<int>(m_project.historyMark()); }
    // Undoes or redoes, one step at a time through undo() / redo(), until `position` steps are
    // done (0 = before the first step). False for a position outside 0..steps, while recording,
    // or when a step refuses (it then stops where it got to).
    Q_INVOKABLE bool jumpToHistory(int position);
    Q_INVOKABLE int addAudioTrack();
    Q_INVOKABLE int addVocalTrack();
    Q_INVOKABLE int addInstrumentTrack();
    Q_INVOKABLE int addInstrumentTrackOfType(const QString& instrumentType);
    Q_INVOKABLE bool removeTrack(int trackId);
    Q_INVOKABLE bool moveTrack(int trackId, int destinationRow);
    Q_INVOKABLE bool renameTrack(int trackId, const QString& name);
    Q_INVOKABLE bool setTrackColour(int trackId, const QString& colour);
    Q_INVOKABLE int indexOfTrack(int trackId) const { return rowForTrackId(trackId); }
    Q_INVOKABLE int trackIdAt(int row) const;
    Q_INVOKABLE bool setSolo(int trackId, bool enabled);
    Q_INVOKABLE bool setMuted(int trackId, bool enabled);
    Q_INVOKABLE bool setRecordDry(int trackId, bool dry);
    Q_INVOKABLE bool setRecordArmed(int trackId, bool enabled);
    Q_INVOKABLE bool setMonitorEnabled(int trackId, bool enabled);
    bool cleanMonitorArmed() const;
    QString cleanMonitorInput() const;
    double cleanMonitorGain() const;
    QVariantList cleanMonitorEqBands() const;
    Q_INVOKABLE bool setEqBand(int trackId, int band, double gainDb);
    // Chaos EQ (plan 6.2), for ChaosEqWrapper. A change emits chaosEqChanged, not dataChanged:
    // the engine updates the track's EQ processors without rebuilding playback on every drag.
    const chaoseq::Settings* chaosEq(int trackId) const;
    bool setChaosEq(int trackId, const chaoseq::Settings& settings);
    // Harmonium tracks, for HarmoniumWrapper, the same way: harmoniumChanged, not dataChanged.
    const harmonium::Settings* harmonium(int trackId) const;   // null unless a Harmonium track
    bool setHarmonium(int trackId, const harmonium::Settings& settings);
    // Drums tracks, for DrumKitWrapper, the same way: drumKitChanged.
    const drumkit::Settings* drumKit(int trackId) const;       // null unless a Drums track
    bool setDrumKit(int trackId, const drumkit::Settings& settings);
    // Electronic Drums tracks, for ElectroDrumsWrapper, the same way: electroDrumsChanged.
    const electrodrums::Settings* electroDrums(int trackId) const;   // null unless Electronic Drums
    bool setElectroDrums(int trackId, const electrodrums::Settings& settings);
    // Piano tracks, for PianoWrapper, the same way: pianoChanged.
    const piano::Settings* piano(int trackId) const;             // null unless a Piano or Chords track
    bool setPiano(int trackId, const piano::Settings& settings);
    // Chords tracks (strips, autoplay, key), for ChordsWrapper: chordsChanged.
    const chords::Settings* chords(int trackId) const;           // null unless a Chords track
    bool setChords(int trackId, const chords::Settings& settings);
    // Mixing plugins (restMap G7), for MixEffectWrapper the same way: mixEffectChanged, not
    // dataChanged. Not undoable.
    std::optional<mixfx::Settings> mixEffect(int trackId, std::string_view type) const;
    bool setMixEffect(int trackId, const QString& type, const mixfx::Settings& settings);
    Q_INVOKABLE bool isMixEffect(const QString& type) const;
    Q_INVOKABLE bool resetTrackEq(int trackId);
    bool newTrackRecordDry() const { return m_newTrackRecordDry; }
    void setNewTrackRecordDry(bool dry);
    // Insert effects (plan 6.1). Rules live in the backend (effectchain); not undoable.
    Q_INVOKABLE int addTrackEffect(int trackId, const QString& type); // new effect id, or -1
    Q_INVOKABLE bool removeTrackEffect(int trackId, int effectId);
    Q_INVOKABLE bool moveTrackEffect(int trackId, int effectId, int toIndex);
    // What the "Add FX" picker offers for this track: [{type, title, subtitle}].
    Q_INVOKABLE QVariantList addableEffects(int trackId) const;
    QStringList eqPresetNames() const;
    QString eqPresetError() const { return m_eqPresetError; }
    Q_INVOKABLE bool applyEqPreset(int trackId, int preset);
    Q_INVOKABLE bool saveEqPreset(int trackId, const QUrl& file);
    Q_INVOKABLE bool loadEqPreset(int trackId, const QUrl& file);
    Q_INVOKABLE bool setTrackInputPort(int trackId, const QString& port);
    Q_INVOKABLE bool setVolume(int trackId, double volume);
    // -1 left .. 0 centre .. +1 right; clamped by the backend.
    Q_INVOKABLE bool setPan(int trackId, double pan);
    // Mixer strip width in px (clamped by the backend); saved with the project.
    Q_INVOKABLE bool setStripWidth(int trackId, double width);
    double masterVolume() const { return m_state.masterVolume(); }
    Q_INVOKABLE bool setMasterVolume(double volume);
    Q_INVOKABLE void resetMasterVolume();
    int masterTrackId() const { return Project::MasterTrackId; }
    QVariantList masterEffects() const;
    QVariantList masterEqBands() const;
    bool masterMuted() const { return m_state.masterMuted(); }
    Q_INVOKABLE bool setMasterMuted(bool muted);
    Q_INVOKABLE int addAudioClip(int trackId, const QString& poolId,
                                 qint64 startSample, qint64 lengthSamples,
                                 qint64 sourceOffset = 0, bool effectsPrinted = true);
    Q_INVOKABLE int addClipAtBeat(int trackId, double startBeat, double lengthBeats);
    Q_INVOKABLE bool trimClipStartAtBeat(int trackId, int clipId, double startBeat);
    Q_INVOKABLE int splitClipAtBeat(int trackId, int clipId, double beat);
    Q_INVOKABLE bool editClips(const QVariantList& ids, const QString& operation, double deltaBeats = 0, int laneDelta = 0);
    Q_INVOKABLE bool canMergeClips(int trackId, int first, int second) const;
    // Clip tools (restMap G14) on clip ids, one undo step: "reverse", "invertPolarity",
    // "normalise" (peak to 0 dBFS). Audio clips only; can... is false with none among them.
    Q_INVOKABLE bool canApplyClipTool(const QVariantList& ids) const;
    // Clip effects (restMap R2). A clip's effects id works with the effect calls (addTrackEffect,
    // setEqBand, the mixing plugins...). clipEffectsInfo: {effectsId, trackId, name, effects,
    // eqBands}, empty for a pattern clip or none; it changes with clipsChanged.
    Q_INVOKABLE int clipEffectsId(int clipId) const;
    Q_INVOKABLE bool canHaveClipEffects(int clipId) const;
    Q_INVOKABLE QVariantMap clipEffectsInfo(int clipId) const;
    // Splits the track's audio clip under the range at its edges (one undo step); the piece
    // inside, or -1.
    Q_INVOKABLE int splitForRangeEffects(int trackId, double startBeat, double endBeat);
    // Import (restMap E2), for ImportExportWrapper: decoded files kept where their paths say, as
    // clips at startBeat on new tracks (targetTrackId < 0) or on one track; one undo step.
    std::vector<int> importAudio(const std::vector<std::shared_ptr<const AudioData>>& files, double startBeat, int targetTrackId);
    Q_INVOKABLE bool applyClipTool(const QVariantList& ids, const QString& tool);
    Q_INVOKABLE bool canMergeSelectedClips(const QVariantList& ids) const;
    Q_INVOKABLE bool mergeSelectedClips(const QVariantList& ids);
    Q_INVOKABLE bool mergeClips(int trackId, int first, int second);
    Q_INVOKABLE bool moveClipAtBeat(int trackId, int clipId, double startBeat);
    Q_INVOKABLE bool resizeClipAtBeat(int trackId, int clipId, double lengthBeats);
    Q_INVOKABLE bool removeAudioClip(int trackId, int clipId);
    bool hasClipboard() const { return m_state.hasClipboard(); }
    int clipboardClipCount() const { return m_state.clipboardClipCount(); }
    Q_INVOKABLE bool copyClips(const QVariantList& ids);
    // ripple (restMap G15): close the gaps deletes leave / make room for pastes and duplicates.
    Q_INVOKABLE QVariantList cutClips(const QVariantList& ids, bool ripple = false);
    Q_INVOKABLE bool deleteClips(const QVariantList& ids, bool ripple = false);
    Q_INVOKABLE QVariantList pasteClips(int targetTrackId, double targetBeat, bool ripple = false);
    Q_INVOKABLE QVariantList duplicateClips(const QVariantList& ids, double beatOffset = 0.0, bool ripple = false);
    // Time selection edits (restMap G15) on every track, clips and volume-envelope points.
    Q_INVOKABLE bool copyRange(double startBeat, double endBeat);
    Q_INVOKABLE bool cutRange(double startBeat, double endBeat, bool ripple);
    Q_INVOKABLE bool deleteRange(double startBeat, double endBeat, bool ripple);
    Q_INVOKABLE void setTempo(double bpm, int numerator, int denominator);

    // Volume envelope (restMap G6), in project beats. Points, moves and bypass are undoable (one
    // drag = one undo step with continueEdit); shown and over clips are view state.
    Q_INVOKABLE int addEnvelopePoint(int trackId, double beat, double level);   // index, or -1
    Q_INVOKABLE bool moveEnvelopePoint(int trackId, int index, double beat, double level, bool continueEdit = false);
    Q_INVOKABLE bool removeEnvelopePoint(int trackId, int index);
    // Undoable; false for an unknown shape id, a bad track or point, or while recording.
    Q_INVOKABLE bool setEnvelopePointShape(int trackId, int index, const QString& shape);
    QVariantList envelopeShapes() const;
    // What the line is at a beat, and the line to draw over a range: [QPointF(beat, level)]
    // (see TrackList::envelopeCurve), so the drawn curve is the one the engine plays.
    Q_INVOKABLE double envelopeLevelAt(int trackId, double beat) const;
    Q_INVOKABLE QVariantList envelopeCurve(int trackId, double fromBeat, double toBeat, int steps) const;
    Q_INVOKABLE bool setEnvelopeBypassed(int trackId, bool bypassed);
    Q_INVOKABLE bool setEnvelopeShown(int trackId, bool shown);
    Q_INVOKABLE bool toggleEnvelopeShown(int trackId);
    Q_INVOKABLE bool setEnvelopeOverClips(int trackId, bool overClips);
    // The fader-shaped scale: a level's dB and its readout ("-inf dB", "+1.5 dB", "-12.0 dB").
    Q_INVOKABLE double envelopeDb(double level) const;
    Q_INVOKABLE QString envelopeDbText(double level) const;
    double envelopeUnityLevel() const;

    double trackRowHeight() const { return m_layout.trackHeight(); }
    void setTrackRowHeight(double height);
    double envelopeLaneHeight() const { return m_layout.envelopeHeight(); }
    void setEnvelopeLaneHeight(double height);
    QVariantList rowTops() const;
    double layoutHeight() const { return m_layout.totalHeight(); }
    // Pointer y (from the top of the first row) -> row, its envelope lane included; past the
    // rows it continues at trackRowHeight steps.
    Q_INVOKABLE int rowAtY(double y) const { return m_layout.rowAt(y); }
    Q_INVOKABLE bool inEnvelopeLane(double y) const { return m_layout.inEnvelopeLane(y); }
    // y in the row units the timeline's box selection uses (see TrackLayout::rowCoordinate).
    Q_INVOKABLE double rowCoordinate(double y) const { return m_layout.rowCoordinate(y); }

signals:
    void selectedTrackChanged(int trackId);
    void selectedKeyShiftChanged(int semitones);
    void selectedClipChanged(int clipId);
    void clipboardChanged();
    void historyChanged();
    void undoHistoryChanged();
    void projectChanged();
    void chaosEqChanged(int trackId);
    void harmoniumChanged(int trackId);
    void drumKitChanged(int trackId);
    void electroDrumsChanged(int trackId);
    void pianoChanged(int trackId);
    void chordsChanged(int trackId);
    void mixEffectChanged(int trackId, const QString& type);
    void eqPresetErrorChanged();
    void countChanged();
    void contentEndBeatsChanged();
    void clipsChanged();
    void cleanMonitorChanged(bool enabled);
    void cleanMonitorInputChanged();
    void cleanMonitorGainChanged(double gain);
    void cleanMonitorEqChanged();
    void newTrackRecordDryChanged();
    void masterVolumeChanged();
    void masterMutedChanged();
    void layoutChanged();
    void masterChanged();

private:
    void notifyContentEdit();   // clips and envelopes on any track (G15)
    void notifyClipEdit(int trackId);
    // Clip gain only: refresh the track's clip data, engine and history, but not the clip list,
    // so the timeline does not rebuild every clip block while a gain fader moves.
    void notifyClipGainEdit(int trackId);
    // After a track row was added or removed (remove, undo of a removal, redo).
    void notifyTrackCountChanged();
    void setEqPresetError(const QString& error);
    bool applyEqBands(int trackId, const std::array<float,10>& bands);
    void syncSelectionState();
    void syncLayout();
    void notifyEnvelope(int trackId, const QList<int>& roles);
    TrackLayout m_layout;
    QString m_eqPresetError;
    int rowForTrackId(int trackId) const;
    int appendTrack(const std::function<int()>& insert);
    bool notifyTrackChanged(int trackId, int role, TrackList::Change change);

    TrackList m_state;
    // These aliases keep the batch-6 serialization/snapshot adapters on the same backend state.
    Project& m_project = m_state.project();
    bool m_newTrackRecordDry = false;
    TrackList::AudioMap& m_audio = m_state.audio();
    TrackList::MediaPaths& m_mediaPaths = m_state.mediaPaths();
    int m_notifiedTrackId = -1;
    int m_notifiedKeyShift = 0;
    void syncKeyShift();
    int m_notifiedClipId = -1;
    // The history as last announced (restMap G16): undoHistoryChanged only when it differs.
    void syncUndoHistory();
    std::uint64_t m_notifiedSteps = 0;
    std::size_t m_notifiedHistorySize = 0, m_notifiedHistoryMark = 0;
};
