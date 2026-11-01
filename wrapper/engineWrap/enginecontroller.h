#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <QPointer>
#include "wrapper/session/tracklistmodel.h"
class TakeRecorder;

class GuitarSuiteModel;
class GuitarSuite;
struct EngineSnapshot;

class QQmlEngine;
class QJSEngine;
class AudioEngine;
class AudioDevice;

// The engine in QML. Phase 1 adds the AudioEngine here: start, stop, xrun count and the test tone.
class EngineController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool playing READ playing NOTIFY transportChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY transportChanged)
    Q_PROPERTY(bool takeBusy READ takeBusy NOTIFY transportChanged)
    Q_PROPERTY(double positionBeats READ positionBeats NOTIFY transportChanged)
    Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY transportChanged)
    Q_PROPERTY(double recordingStartBeats READ recordingStartBeats NOTIFY transportChanged)
    Q_PROPERTY(int recordingTrackId READ recordingTrackId NOTIFY transportChanged)
    Q_PROPERTY(int numerator READ numerator NOTIFY meterChanged)
    Q_PROPERTY(int denominator READ denominator NOTIFY meterChanged)
    Q_PROPERTY(bool repeating READ repeating NOTIFY transportChanged)
    Q_PROPERTY(QString transportMessage READ transportMessage NOTIFY transportMessageChanged)
    Q_PROPERTY(bool restoreSessionOnStartup READ restoreSessionOnStartup WRITE setRestoreSessionOnStartup NOTIFY restoreSessionOnStartupChanged)
    Q_PROPERTY(QVariantMap monitorLevels READ monitorLevels NOTIFY monitorLevelsChanged)
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

public:
    // Inject a device for offline integration checks; system discovery is only for JACK.
    EngineController(std::unique_ptr<AudioDevice> device, bool discoverSystemDevices, QObject* parent = nullptr);
    static EngineController* instance();
    static EngineController* create(QQmlEngine*, QJSEngine*);
    ~EngineController() override;

    bool playing() const;
    bool recording() const;
    bool takeBusy() const { return bool(m_take); }
    double positionBeats() const;
    double positionSeconds() const;
    double recordingStartBeats() const { return m_recordStartBeat; }
    int recordingTrackId() const { return m_recordTrack; }
    int numerator() const { return m_numerator; }
    int denominator() const { return m_denominator; }
    bool repeating() const { return m_repeating; }
    QString transportMessage() const { return m_transportMessage; }
    bool restoreSessionOnStartup() const;
    void setRestoreSessionOnStartup(bool enabled);
    Q_INVOKABLE void attachProject(TrackListModel* model);
    Q_INVOKABLE void transportAction(const QString& action);
    Q_INVOKABLE void seekBeat(double beat);
    Q_INVOKABLE void setMeter(int numerator, int denominator);
    Q_INVOKABLE bool saveSession();
    Q_INVOKABLE void shutdownSession();
    QVariantMap monitorLevels() const { return m_monitorLevels; }
    Q_INVOKABLE void clearMonitorClips();
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
    Q_INVOKABLE void setCleanMonitorEnabled(bool enabled);
    Q_INVOKABLE void setCleanMonitorGain(double gain);
    Q_INVOKABLE void setCleanMonitorEqBands(const QVariantList& bands);
    Q_INVOKABLE bool selectInput(int index);
    Q_INVOKABLE bool setOutputDevice(int index);
    Q_INVOKABLE bool setBufferSize(int frames);
    Q_INVOKABLE QString inputPortForDevice(int index) const;
    Q_INVOKABLE bool setCleanMonitorInput(const QString& port);

signals:
    void transportChanged();
    void transportMessageChanged();
    void restoreSessionOnStartupChanged();
    void meterChanged();
    void monitorLevelsChanged();
    void bpmChanged();
    void statusChanged();
    void audioRoutingChanged();
    void outputDevicesChanged();
    void outputDeviceChanged();
    void inputDevicesChanged();
    void bufferSizeChanged();

private:
    explicit EngineController(QObject* parent);
    void setStatus(const QString& status);
    void updateAudioStatus();
    void discoverOutputDevices();
    void discoverInputDevices();
    void connectOutputDevicePorts(const QString& description);
    void queueGuitarSuite(std::shared_ptr<GuitarSuite> chain);

    void refreshProject();
    void pollTransport();
    void completeTake();
    void setTransportMessage(const QString& message);
    bool postTransport(int type, qint64 a = 0, qint64 b = 0);
    QPointer<TrackListModel> m_project;
    std::unique_ptr<TakeRecorder> m_take;
    QTimer m_sessionTimer;
    bool m_repeating = false, m_shutdown = false, m_restoring = false, m_sessionSaveBlocked = false, m_restoringPreset = false;
    int m_recordTrack = -1, m_numerator = 4, m_denominator = 4;
    double m_recordStartBeat = 0;
    QString m_transportMessage;
    QString m_status = QStringLiteral("not started");
    std::unique_ptr<AudioEngine> m_engine;
    GuitarSuiteModel* m_guitarSuite = nullptr;
    std::unique_ptr<EngineSnapshot> m_pendingGuitarSnapshot;
    QTimer m_snapshotTimer;
    QTimer m_meterTimer;
    QVariantMap m_monitorLevels;
    double m_bpm = 120.0;
    bool m_audioReady = false;
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
