#pragma once

#include "session/project.h"

#include <QAbstractListModel>
#include <QVariantList>
#include <QUrl>
#include <QStringList>
#include <QJsonObject>
#include <QSet>
#include "engine/enginesnapshot.h"
#include <QtQml/qqmlregistration.h>

class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList eqPresetNames READ eqPresetNames CONSTANT)
    Q_PROPERTY(QString eqPresetError READ eqPresetError NOTIFY eqPresetErrorChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(double contentEndBeats READ contentEndBeats NOTIFY contentEndBeatsChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool cleanMonitorArmed READ cleanMonitorArmed NOTIFY cleanMonitorChanged)
    Q_PROPERTY(QString cleanMonitorInput READ cleanMonitorInput NOTIFY cleanMonitorInputChanged)
    Q_PROPERTY(double cleanMonitorGain READ cleanMonitorGain NOTIFY cleanMonitorGainChanged)
    Q_PROPERTY(QVariantList cleanMonitorEqBands READ cleanMonitorEqBands NOTIFY cleanMonitorEqChanged)

public:
    enum Role {
        TrackIdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        TypeRole,
        SoloRole,
        MutedRole,
        RecordArmedRole,
        InputPortRole,
        VolumeRole,
        ClipsRole,
        EqBandsRole,
        LiveInputRole,
        RecordDryRole,
        RecordsWithFxRole,
        ColourRole
    };
    Q_ENUM(Role)

    explicit TrackListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int count() const { return rowCount(); }
    double contentEndBeats() const { return m_project.contentEndBeats(); }
    QVariantList clips() const;
    Q_INVOKABLE QVariantList clipWaveform(int trackId, int clipId, int bins, double from = 0, double to = 1) const;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    QJsonObject sessionState() const;
    bool restoreSessionState(const QJsonObject& state, QString& error);
    void adoptDocument(TrackListModel& candidate);
    std::unique_ptr<EngineSnapshot> playbackSnapshot() const;
    bool attachTake(int trackId, std::int64_t start, std::shared_ptr<const AudioData> audio, bool effectsPrinted = true);
    int recordingTrackId() const;
    const Project& project() const { return m_project; }
    void setSampleRate(double rate);
    void lockRecordingTrack(int id) { lockRecordingTracks(id<0 ? QSet<int>{} : QSet<int>{id}); }
    void lockRecordingTracks(const QSet<int>& ids) { m_recordingTracks=ids; emit historyChanged(); }
    bool canUndo() const { return m_recordingTracks.isEmpty() && m_project.canUndo(); }
    bool canRedo() const { return m_recordingTracks.isEmpty() && m_project.canRedo(); }
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE int addAudioTrack();
    Q_INVOKABLE int addVocalTrack();
    Q_INVOKABLE int addInstrumentTrack();
    Q_INVOKABLE int addInstrumentTrackOfType(const QString& instrumentType);
    Q_INVOKABLE bool removeTrack(int trackId);
    Q_INVOKABLE bool renameTrack(int trackId, const QString& name);
    Q_INVOKABLE bool setTrackColour(int trackId, const QString& colour);
    Q_INVOKABLE int indexOfTrack(int trackId) const { return rowForTrackId(trackId); }
    Q_INVOKABLE int trackIdAt(int row) const;
    Q_INVOKABLE bool setSolo(int trackId, bool enabled);
    Q_INVOKABLE bool setMuted(int trackId, bool enabled);
    Q_INVOKABLE bool setRecordDry(int trackId, bool dry);
    Q_INVOKABLE bool setRecordArmed(int trackId, bool enabled);
    bool cleanMonitorArmed() const;
    QString cleanMonitorInput() const;
    double cleanMonitorGain() const;
    QVariantList cleanMonitorEqBands() const;
    Q_INVOKABLE bool setEqBand(int trackId, int band, double gainDb);
    Q_INVOKABLE bool resetTrackEq(int trackId);
    QStringList eqPresetNames() const;
    QString eqPresetError() const { return m_eqPresetError; }
    Q_INVOKABLE bool applyEqPreset(int trackId, int preset);
    Q_INVOKABLE bool saveEqPreset(int trackId, const QUrl& file);
    Q_INVOKABLE bool loadEqPreset(int trackId, const QUrl& file);
    Q_INVOKABLE bool setTrackInputPort(int trackId, const QString& port);
    Q_INVOKABLE bool setVolume(int trackId, double volume);
    Q_INVOKABLE int addAudioClip(int trackId, const QString& poolId,
                                 qint64 startSample, qint64 lengthSamples,
                                 qint64 sourceOffset = 0, bool effectsPrinted = true);
    Q_INVOKABLE int addClipAtBeat(int trackId, double startBeat, double lengthBeats);
    Q_INVOKABLE bool trimClipStartAtBeat(int trackId, int clipId, double startBeat);
    Q_INVOKABLE int splitClipAtBeat(int trackId, int clipId, double beat);
    Q_INVOKABLE bool moveClipAtBeat(int trackId, int clipId, double startBeat);
    Q_INVOKABLE bool resizeClipAtBeat(int trackId, int clipId, double lengthBeats);
    Q_INVOKABLE bool removeAudioClip(int trackId, int clipId);
    Q_INVOKABLE void setTempo(double bpm, int numerator, int denominator);

signals:
    void historyChanged();
    void projectChanged();
    void eqPresetErrorChanged();
    void countChanged();
    void contentEndBeatsChanged();
    void clipsChanged();
    void cleanMonitorChanged(bool enabled);
    void cleanMonitorInputChanged();
    void cleanMonitorGainChanged(double gain);
    void cleanMonitorEqChanged();

private:
    void notifyClipEdit(int trackId);
    void setEqPresetError(const QString& error);
    bool applyEqBands(int trackId, const std::array<float,10>& bands);
    QString m_eqPresetError;
    int rowForTrackId(int trackId) const;
    int appendTrack(TrackKind kind, const QString& name, const QString& type);
    bool notifyTrackChanged(int trackId, int role, bool changed);

    Project m_project;
    QHash<QString, std::shared_ptr<const AudioData>> m_audio;
    QSet<QString> m_mediaPaths;
    QSet<int> m_recordingTracks;
    int m_nextAudioNumber = 1;
    int m_nextVocalNumber = 1;
    int m_nextInstrumentNumber = 1;
    QHash<QString, int> m_instrumentNumbers;
    int m_nextClipNumber = 1;
};
