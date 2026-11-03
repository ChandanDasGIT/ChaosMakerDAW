#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "engine/audioengine.h"
#include "audiodevice/audiodevice.h"

class AudioEngineWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool audioReady READ isAudioReady NOTIFY audioReadyChanged)
    Q_PROPERTY(double sampleRate READ sampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(int bufferSize READ bufferSize NOTIFY bufferSizeChanged)
    Q_PROPERTY(int latencyFrames READ latencyFrames CONSTANT)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY transportStateChanged)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY transportStateChanged)
    Q_PROPERTY(bool isCountingIn READ isCountingIn NOTIFY transportStateChanged)
    Q_PROPERTY(qint64 positionFrames READ positionFrames NOTIFY positionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)

public:
    explicit AudioEngineWrapper(std::unique_ptr<AudioDevice> device, QObject* parent = nullptr);
    explicit AudioEngineWrapper(QObject* parent = nullptr);
    ~AudioEngineWrapper() override;

    AudioEngine* engine() noexcept { return m_engine.get(); }
    const AudioEngine* engine() const noexcept { return m_engine.get(); }

    bool isAudioReady() const noexcept { return m_audioReady; }
    double sampleRate() const;
    int bufferSize() const;
    int latencyFrames() const;
    QString lastError() const;

    bool isPlaying() const;
    bool isRecording() const;
    bool isCountingIn() const;
    qint64 positionFrames() const;

    // Realtime lifecycle and controls
    Q_INVOKABLE bool start(int wantedBufferFrames = 128);
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool setBufferSize(int frames);
    Q_INVOKABLE bool postCommand(int type, qint64 argA = 0, qint64 argB = 0);

    // Audio device connection
    Q_INVOKABLE bool connectInput(const QString& source);
    Q_INVOKABLE int connectInputChannel(const QString& source);
    Q_INVOKABLE bool connectOutput(const QString& left, const QString& right);

    // Snapshots & metering
    void publishSnapshot(std::unique_ptr<EngineSnapshot> snapshot);
    void publishPlaybackSnapshot(std::unique_ptr<EngineSnapshot> snapshot);
    void publishMonitorSnapshot(std::unique_ptr<EngineSnapshot> snapshot);

    Q_INVOKABLE QVariantMap takeMonitorLevels();
    Q_INVOKABLE void clearMonitorClips();

signals:
    void audioReadyChanged();
    void sampleRateChanged();
    void bufferSizeChanged();
    void transportStateChanged();
    void positionChanged();
    void errorOccurred(const QString& error);

protected:
    void setAudioReady(bool ready);
    std::unique_ptr<AudioEngine> m_engine;
    bool m_audioReady = false;
};
