#pragma once

#include "session/tracklist.h"

#include <QAbstractListModel>
#include <QVariantList>
#include <QUrl>
#include <QStringList>
#include <QJsonObject>
#include <QSet>
#include "engine/enginesnapshot.h"
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
    Q_PROPERTY(int selectedClipId READ selectedClipId WRITE setSelectedClipId NOTIFY selectedClipChanged)
    Q_PROPERTY(double contentEndBeats READ contentEndBeats NOTIFY contentEndBeatsChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
    Q_PROPERTY(bool hasClipboard READ hasClipboard NOTIFY clipboardChanged)
    Q_PROPERTY(int clipboardClipCount READ clipboardClipCount NOTIFY clipboardChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool cleanMonitorArmed READ cleanMonitorArmed NOTIFY cleanMonitorChanged)
    Q_PROPERTY(QString cleanMonitorInput READ cleanMonitorInput NOTIFY cleanMonitorInputChanged)
    Q_PROPERTY(double cleanMonitorGain READ cleanMonitorGain NOTIFY cleanMonitorGainChanged)
    Q_PROPERTY(QVariantList cleanMonitorEqBands READ cleanMonitorEqBands NOTIFY cleanMonitorEqChanged)
    // Recording mode for tracks added from now on, bound to the "default recording mode"
    // preference (plan 6.6). Not set: dry only for plain Audio tracks.
    Q_PROPERTY(bool newTrackRecordDry READ newTrackRecordDry WRITE setNewTrackRecordDry NOTIFY newTrackRecordDryChanged)
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY masterVolumeChanged)
    Q_PROPERTY(bool masterMuted READ masterMuted WRITE setMasterMuted NOTIFY masterMutedChanged)

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
        EffectsRole   // insert effects in chain order: [{id, type, title, subtitle, removable}] (plan 6.1)
    };
    Q_ENUM(Role)

    explicit TrackListWrapper(QObject* parent = nullptr);
    TrackList& domain() { return m_state; }
    const TrackList& domain() const { return m_state; }
    QVariantList trackChoices() const;
    QVariantList instrumentChoices() const;
    Q_INVOKABLE int addTrackChoice(const QString& id);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int count() const { return rowCount(); }
    int selectedTrackId() const { return m_state.selectedTrackId(); }
    Q_INVOKABLE bool selectTrack(int id);
    int selectedClipId() const { return m_state.selectedClipId(); }
    Q_INVOKABLE void setSelectedClipId(int id);
    Q_INVOKABLE void selectClip(int trackId, int clipId);
    // continueEdit: true for every step of a fader drag after the first (one undo step per drag).
    Q_INVOKABLE bool setClipGain(int trackId, int clipId, double gainDb, bool continueEdit = false);
    Q_INVOKABLE double clipGain(int trackId, int clipId) const;
    // Pattern notes of a clip in clip-relative beats: [{id, note, startBeat, lengthBeats, velocity}]
    Q_INVOKABLE QVariantList clipNotes(int trackId, int clipId) const;
    // C++ adapter used by PianoRollWrapper; one undoable edit, emits clip-edit signals on change.
    bool setClipNotes(int trackId, int clipId, std::vector<MidiNote> notes);
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
    Q_INVOKABLE bool canMergeSelectedClips(const QVariantList& ids) const;
    Q_INVOKABLE bool mergeSelectedClips(const QVariantList& ids);
    Q_INVOKABLE bool mergeClips(int trackId, int first, int second);
    Q_INVOKABLE bool moveClipAtBeat(int trackId, int clipId, double startBeat);
    Q_INVOKABLE bool resizeClipAtBeat(int trackId, int clipId, double lengthBeats);
    Q_INVOKABLE bool removeAudioClip(int trackId, int clipId);
    bool hasClipboard() const { return m_state.hasClipboard(); }
    int clipboardClipCount() const { return m_state.clipboardClipCount(); }
    Q_INVOKABLE bool copyClips(const QVariantList& ids);
    Q_INVOKABLE QVariantList cutClips(const QVariantList& ids);
    Q_INVOKABLE QVariantList pasteClips(int targetTrackId, double targetBeat);
    Q_INVOKABLE QVariantList duplicateClips(const QVariantList& ids, double beatOffset = 0.0);
    Q_INVOKABLE void setTempo(double bpm, int numerator, int denominator);

signals:
    void selectedTrackChanged(int trackId);
    void selectedClipChanged(int clipId);
    void clipboardChanged();
    void historyChanged();
    void projectChanged();
    void chaosEqChanged(int trackId);
    void harmoniumChanged(int trackId);
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

private:
    void notifyClipEdit(int trackId);
    // Clip gain only: refresh the track's clip data, engine and history, but not the clip list,
    // so the timeline does not rebuild every clip block while a gain fader moves.
    void notifyClipGainEdit(int trackId);
    // After a track row was added or removed (remove, undo of a removal, redo).
    void notifyTrackCountChanged();
    void setEqPresetError(const QString& error);
    bool applyEqBands(int trackId, const std::array<float,10>& bands);
    void syncSelectionState();
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
    int m_notifiedClipId = -1;
};
