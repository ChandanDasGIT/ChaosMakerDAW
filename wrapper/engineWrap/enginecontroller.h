#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <map>
#include <QPointer>
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
class TakeRecorder;
struct RecordingGroup;
#include "recording/recordingcontroller.h"
#include "session/projectsession.h"

class GuitarSuiteEditorWrapper;
using GuitarSuiteModel = GuitarSuiteEditorWrapper;
class GuitarSuite;
class TrackInput;
struct EngineSnapshot;
#include "engine/snapshotbuilder.h"

class QQmlEngine;
class QJSEngine;
class AudioEngine;
class AudioDevice;

#include "audioenginewrapper.h"

// The engine in QML. Phase 1 adds the AudioEngine here: start, stop, xrun count and the test tone.
class EngineController : public AudioEngineWrapper {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool playing READ playing NOTIFY transportChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY transportChanged)
    Q_PROPERTY(bool countInEnabled READ countInEnabled WRITE setCountInEnabled NOTIFY countInSettingsChanged)
    Q_PROPERTY(bool countingIn READ countingIn NOTIFY transportChanged)
    Q_PROPERTY(int countInBeatsRemaining READ countInBeatsRemaining NOTIFY transportChanged)
    Q_PROPERTY(bool takeBusy READ takeBusy NOTIFY transportChanged)
    Q_PROPERTY(double positionBeats READ positionBeats NOTIFY transportChanged)
    Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY transportChanged)
    Q_PROPERTY(double recordingStartBeats READ recordingStartBeats NOTIFY transportChanged)
    Q_PROPERTY(int recordingTrackId READ recordingTrackId NOTIFY transportChanged)
    Q_PROPERTY(QVariantList recordingTracks READ recordingTracks NOTIFY transportChanged)
    Q_PROPERTY(int numerator READ numerator NOTIFY meterChanged)
    Q_PROPERTY(int denominator READ denominator NOTIFY meterChanged)
    Q_PROPERTY(bool repeating READ repeating NOTIFY transportChanged)
    Q_PROPERTY(bool metronomeEnabled READ metronomeEnabled WRITE setMetronomeEnabled NOTIFY metronomeChanged)
    Q_PROPERTY(double metronomeVolume READ metronomeVolume WRITE setMetronomeVolume NOTIFY metronomeChanged)
    Q_PROPERTY(QString transportMessage READ transportMessage NOTIFY transportMessageChanged)
    Q_PROPERTY(bool restoreSessionOnStartup READ restoreSessionOnStartup WRITE setRestoreSessionOnStartup NOTIFY restoreSessionOnStartupChanged)
    Q_PROPERTY(QVariantMap monitorLevels READ monitorLevels NOTIFY monitorLevelsChanged)
    Q_PROPERTY(QVariantMap trackLevels READ trackLevels NOTIFY trackLevelsChanged)
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY bpmChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList inputPorts READ inputPorts CONSTANT)
    Q_PROPERTY(QStringList inputDevices READ inputDevices NOTIFY inputDevicesChanged)
    Q_PROPERTY(QStringList outputDevices READ outputDevices NOTIFY outputDevicesChanged)
    Q_PROPERTY(QStringList bufferSizes READ bufferSizes CONSTANT)
    Q_PROPERTY(int selectedBufferSize READ selectedBufferSize NOTIFY bufferSizeChanged)
    Q_PROPERTY(int selectedInputIndex READ selectedInputIndex NOTIFY audioRoutingChanged)
    Q_PROPERTY(int selectedOutputIndex READ selectedOutputIndex NOTIFY outputDeviceChanged)
    Q_PROPERTY(QObject* guitarSuite READ guitarSuite CONSTANT)
    Q_PROPERTY(int monitoredTrackId READ monitoredTrackId NOTIFY monitoredTrackChanged)
    Q_PROPERTY(QVariantList monitoredTracks READ monitoredTracks NOTIFY monitoredTrackChanged)
    Q_PROPERTY(QUrl projectFile READ projectFile NOTIFY projectFileChanged)
    Q_PROPERTY(bool projectDirty READ projectDirty NOTIFY projectDirtyChanged)
    Q_PROPERTY(bool projectLoading READ projectLoading NOTIFY projectLoadingChanged)
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY masterVolumeChanged)
    Q_PROPERTY(bool masterMuted READ masterMuted WRITE setMasterMuted NOTIFY masterMutedChanged)

public:
    // Inject a device for offline integration checks; system discovery is only for JACK.
    EngineController(std::unique_ptr<AudioDevice> device, bool discoverSystemDevices, QObject* parent = nullptr);
    static EngineController* instance();
    static EngineController* create(QQmlEngine*, QJSEngine*);
    ~EngineController() override;

    bool playing() const;
    bool recording() const;
    bool countInEnabled() const { return m_countInEnabled; }
    void setCountInEnabled(bool enabled);
    bool countingIn() const;
    int countInBeatsRemaining() const;
    bool takeBusy() const { return m_recordingController.isBusy(); }
    QVariantList recordingTracks() const;
    QVariantList monitoredTracks() const;
    double positionBeats() const;
    double positionSeconds() const;
    double recordingStartBeats() const { return m_recordingController.recordStartBeat(); }
    int recordingTrackId() const { return m_recordingController.primaryRecordingTrackId(); }
    int numerator() const { return m_numerator; }
    int denominator() const { return m_denominator; }
    bool repeating() const { return m_repeating; }
    bool metronomeEnabled() const { return m_metronomeEnabled; }
    double metronomeVolume() const { return m_metronomeVolume; }
    void setMetronomeEnabled(bool enabled);
    void setMetronomeVolume(double volume);
    QString transportMessage() const { return m_transportMessage; }
    bool restoreSessionOnStartup() const;
    void setRestoreSessionOnStartup(bool enabled);
    Q_INVOKABLE void attachProject(TrackListWrapper* model);
    Q_INVOKABLE void transportAction(const QString& action);
    Q_INVOKABLE void seekBeat(double beat);
    Q_INVOKABLE bool setRepeatRange(double startBeat, double endBeat);
    Q_INVOKABLE void setMeter(int numerator, int denominator);
    Q_INVOKABLE bool saveSession();
    QUrl projectFile() const {
        return m_projectSession.currentFilePath().empty() ? QUrl() : QUrl::fromLocalFile(QString::fromStdString(m_projectSession.currentFilePath()));
    }
    bool projectDirty() const { return m_projectSession.isDirty(); }
    bool projectLoading() const { return bool(m_loadingProject); }
    Q_INVOKABLE bool saveProject(const QUrl& file = QUrl());
    Q_INVOKABLE bool openProject(const QUrl& file);
    Q_INVOKABLE bool newProject();
    Q_INVOKABLE void shutdownSession();
    QVariantMap monitorLevels() const { return m_monitorLevels; }
    QVariantMap trackLevels() const { return m_trackLevels; }
    Q_INVOKABLE void clearTrackClips(int trackId);
    Q_INVOKABLE void clearMonitorClips();
    double masterVolume() const { return m_masterVolume; }
    Q_INVOKABLE void setMasterVolume(double volume);
    bool masterMuted() const { return m_masterMuted; }
    Q_INVOKABLE void setMasterMuted(bool muted);
    double bpm() const { return m_bpm; }
    void setBpm(double bpm);
    QString version() const;
    QString status() const { return m_status; }
    QStringList inputPorts() const { return m_inputPorts; }
    QStringList inputDevices() const { return m_inputDevices; }
    QStringList outputDevices() const { return m_outputDevices; }
    QStringList bufferSizes() const { return {QStringLiteral("32"), QStringLiteral("64"), QStringLiteral("128"),
                                               QStringLiteral("256"), QStringLiteral("512"), QStringLiteral("1024")}; }
    int selectedBufferSize() const;
    int selectedInputIndex() const { return m_selectedInputIndex; }
    int selectedOutputIndex() const { return m_selectedOutputIndex; }
    QObject* guitarSuite() const;
    int monitoredTrackId() const { return m_monitorEnabled ? m_monitoredTrack : -1; }
    Q_INVOKABLE QObject* trackEffects(int trackId);
    Q_INVOKABLE void setCleanMonitorEnabled(bool enabled);
    Q_INVOKABLE void setCleanMonitorGain(double gain);
    Q_INVOKABLE void setCleanMonitorEqBands(const QVariantList& bands);
    Q_INVOKABLE bool selectInput(int index);
    Q_INVOKABLE bool setOutputDevice(int index);
    Q_INVOKABLE bool setBufferSize(int frames);
    Q_INVOKABLE QString inputPortForDevice(int index) const;
    Q_INVOKABLE bool setCleanMonitorInput(const QString& port);
    Q_INVOKABLE void sendLiveNoteOn(int trackId, int note, int velocity = 100);
    Q_INVOKABLE void sendLiveNoteOff(int trackId, int note);

signals:
    void projectFileChanged();
    void projectDirtyChanged();
    void projectLoadingChanged();
    void transportChanged();
    void transportMessageChanged();
    void restoreSessionOnStartupChanged();
    void meterChanged();
    void monitorLevelsChanged();
    void trackLevelsChanged();
    void monitoredTrackChanged();
    void bpmChanged();
    void metronomeChanged();
    void countInSettingsChanged();
    void statusChanged();
    void audioRoutingChanged();
    void outputDevicesChanged();
    void outputDeviceChanged();
    void inputDevicesChanged();
    void bufferSizeChanged();
    void masterVolumeChanged();
    void masterMutedChanged();

private:
    void setProjectDirty(bool dirty);
    QJsonObject projectDocument();
    void finishProjectLoad();
    void cancelProjectLoad();
    void bindTrackEffects(int trackId, GuitarSuiteModel* model);
    QUrl m_loadingFile;
    ProjectSession m_projectSession;
    std::unique_ptr<TrackListWrapper> m_loadingProject;
    std::vector<GuitarSuiteModel*> m_loadingEffects;
    QString m_loadingWarning;
    explicit EngineController(QObject* parent);
    void setStatus(const QString& status);
    void updateAudioStatus();
    void discoverOutputDevices();
    void discoverInputDevices();
    void connectOutputDevicePorts(const QString& description);
    void queueGuitarSuite(std::shared_ptr<GuitarSuite> chain);
    void syncTrackEffects();
    void clearTrackEffects();
    void watchEffects(GuitarSuiteModel* model);
    bool effectsBusy() const;
    struct TrackEffects : public SnapshotBuilder::TrackEffects {
        GuitarSuiteModel* model = nullptr;
    };
    std::map<int, TrackEffects> m_trackEffects;
    int m_monitoredTrack = -1;
    QString m_trackRoutingError;
    bool m_bindingEffects = false;

    void refreshProject();
    void publishPlayback();
    void pollTransport();
    void completeTake();
    void setTransportMessage(const QString& message);
    bool postRepeatLoop();
    bool postMetronome(bool enabled, double volume);
    bool postTransport(int type, qint64 a = 0, qint64 b = 0);
    QPointer<TrackListWrapper> m_project;
    RecordingController m_recordingController;
    QTimer m_sessionTimer;
    bool m_repeating = false, m_shutdown = false, m_restoring = false, m_sessionSaveBlocked = false, m_restoringPreset = false;
    int m_numerator = 4, m_denominator = 4;
    double m_repeatStartBeat = 0, m_repeatEndBeat = 0;
    double m_masterVolume = 1.0;
    bool m_masterMuted = false;
    QString m_transportMessage;
    QString m_status = QStringLiteral("not started");
    GuitarSuiteModel* m_guitarSuite = nullptr;
    QTimer m_snapshotTimer;
    QTimer m_meterTimer;
    QVariantMap m_monitorLevels;
    QVariantMap m_trackLevels;
    double m_bpm = 120.0;
    bool m_metronomeEnabled = false, m_countInEnabled = false, m_countInMessagePending = false;
    double m_metronomeVolume = 0.5;
    bool m_monitorEnabled = false;
    bool m_inputConnected = false;
    bool m_outputConnected = false;
    QStringList m_inputPorts;
    QStringList m_inputDevices;
    QStringList m_inputDevicePorts;
    QStringList m_outputPorts;
    QStringList m_outputDevices;
    QStringList m_outputDeviceNames;
    QString m_selectedInput;
    QString m_activeInput;
    QString m_monitorInputOverride;
    QString m_selectedOutputLeft;
    QString m_selectedOutputRight;
    QString m_connectionError;
    int m_selectedInputIndex = -1;
    int m_selectedOutputIndex = -1;
};
