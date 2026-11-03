#include "audioenginewrapper.h"

AudioEngineWrapper::AudioEngineWrapper(std::unique_ptr<AudioDevice> device, QObject* parent)
    : QObject(parent)
{
    if (device) {
        m_engine = std::make_unique<AudioEngine>(std::move(device));
    }
}

AudioEngineWrapper::AudioEngineWrapper(QObject* parent)
    : QObject(parent)
{
}

AudioEngineWrapper::~AudioEngineWrapper() = default;

void AudioEngineWrapper::setAudioReady(bool ready)
{
    if (m_audioReady != ready) {
        m_audioReady = ready;
        emit audioReadyChanged();
    }
}

double AudioEngineWrapper::sampleRate() const
{
    return m_engine ? m_engine->sampleRate() : 48000.0;
}

int AudioEngineWrapper::bufferSize() const
{
    return latencyFrames();
}

int AudioEngineWrapper::latencyFrames() const
{
    return m_engine ? m_engine->latency().requestedBufferFrames : 128;
}

QString AudioEngineWrapper::lastError() const
{
    return m_engine ? QString::fromStdString(m_engine->lastError()) : QString();
}

bool AudioEngineWrapper::isPlaying() const
{
    return m_engine && m_engine->playing();
}

bool AudioEngineWrapper::isRecording() const
{
    return m_engine && m_engine->recording();
}

bool AudioEngineWrapper::isCountingIn() const
{
    return m_engine && m_engine->countInSamplesRemaining() > 0;
}

qint64 AudioEngineWrapper::positionFrames() const
{
    return m_engine ? m_engine->position() : 0;
}

bool AudioEngineWrapper::start(int wantedBufferFrames)
{
    if (!m_engine)
        return false;
    bool ok = m_engine->start(wantedBufferFrames);
    setAudioReady(ok);
    if (ok) {
        emit sampleRateChanged();
        emit bufferSizeChanged();
    } else {
        emit errorOccurred(lastError());
    }
    return ok;
}

void AudioEngineWrapper::stop()
{
    if (m_engine) {
        m_engine->stop();
    }
    setAudioReady(false);
}

bool AudioEngineWrapper::setBufferSize(int frames)
{
    if (!m_engine)
        return false;
    bool ok = m_engine->setBufferSize(frames);
    if (ok) {
        emit bufferSizeChanged();
    } else {
        emit errorOccurred(lastError());
    }
    return ok;
}

bool AudioEngineWrapper::postCommand(int type, qint64 argA, qint64 argB)
{
    if (!m_engine)
        return false;
    return m_engine->post({static_cast<EngineCommand::Type>(type), argA, argB});
}

bool AudioEngineWrapper::connectInput(const QString& source)
{
    if (!m_engine)
        return false;
    return m_engine->connectInput(source.toStdString());
}

int AudioEngineWrapper::connectInputChannel(const QString& source)
{
    if (!m_engine)
        return -1;
    return m_engine->connectInputChannel(source.toStdString());
}

bool AudioEngineWrapper::connectOutput(const QString& left, const QString& right)
{
    if (!m_engine)
        return false;
    return m_engine->connectOutput(left.toStdString(), right.toStdString());
}

void AudioEngineWrapper::publishSnapshot(std::unique_ptr<EngineSnapshot> snapshot)
{
    if (m_engine)
        m_engine->publish(std::move(snapshot));
}

void AudioEngineWrapper::publishPlaybackSnapshot(std::unique_ptr<EngineSnapshot> snapshot)
{
    if (m_engine)
        m_engine->publishPlayback(std::move(snapshot));
}

void AudioEngineWrapper::publishMonitorSnapshot(std::unique_ptr<EngineSnapshot> snapshot)
{
    if (m_engine)
        m_engine->publishMonitor(std::move(snapshot));
}

QVariantMap AudioEngineWrapper::takeMonitorLevels()
{
    QVariantMap map;
    if (!m_engine)
        return map;

    auto levels = m_engine->takeMonitorLevels();
    map[QStringLiteral("raw")] = levels.raw;
    map[QStringLiteral("preFx")] = levels.preFx;
    map[QStringLiteral("left")] = levels.left;
    map[QStringLiteral("right")] = levels.right;
    map[QStringLiteral("rawClip")] = levels.rawClip;
    map[QStringLiteral("preFxClip")] = levels.preFxClip;
    map[QStringLiteral("leftClip")] = levels.leftClip;
    map[QStringLiteral("rightClip")] = levels.rightClip;
    return map;
}

void AudioEngineWrapper::clearMonitorClips()
{
    if (m_engine)
        m_engine->clearMonitorClips();
}
