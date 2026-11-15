#include "audioEngineWrap.h"

AudioEngineWrap::AudioEngineWrap(std::unique_ptr<AudioDevice> device, QObject* parent)
    : QObject(parent)
{
    if (device) {
        m_engine = std::make_unique<AudioEngine>(std::move(device));
    }
}

AudioEngineWrap::AudioEngineWrap(QObject* parent)
    : QObject(parent)
{
}

AudioEngineWrap::~AudioEngineWrap() = default;

void AudioEngineWrap::setAudioReady(bool ready)
{
    if (m_audioReady != ready) {
        m_audioReady = ready;
        emit audioReadyChanged();
    }
}

double AudioEngineWrap::sampleRate() const
{
    return m_engine ? m_engine->sampleRate() : 48000.0;
}

int AudioEngineWrap::bufferSize() const
{
    return latencyFrames();
}

int AudioEngineWrap::latencyFrames() const
{
    return m_engine ? m_engine->latency().requestedBufferFrames : 128;
}

QString AudioEngineWrap::lastError() const
{
    return m_engine ? QString::fromStdString(m_engine->lastError()) : QString();
}

bool AudioEngineWrap::isPlaying() const
{
    return m_engine && m_engine->playing();
}

bool AudioEngineWrap::isRecording() const
{
    return m_engine && m_engine->recording();
}

bool AudioEngineWrap::isCountingIn() const
{
    return m_engine && m_engine->countInSamplesRemaining() > 0;
}

qint64 AudioEngineWrap::positionFrames() const
{
    return m_engine ? m_engine->position() : 0;
}

bool AudioEngineWrap::start(int wantedBufferFrames)
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

void AudioEngineWrap::stop()
{
    if (m_engine) {
        m_engine->stop();
    }
    setAudioReady(false);
}

bool AudioEngineWrap::setBufferSize(int frames)
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

bool AudioEngineWrap::postCommand(int type, qint64 argA, qint64 argB)
{
    if (!m_engine)
        return false;
    return m_engine->post({static_cast<EngineCommand::Type>(type), argA, argB});
}

bool AudioEngineWrap::connectInput(const QString& source)
{
    if (!m_engine)
        return false;
    return m_engine->connectInput(source.toStdString());
}

int AudioEngineWrap::connectInputChannel(const QString& source)
{
    if (!m_engine)
        return -1;
    return m_engine->connectInputChannel(source.toStdString());
}

bool AudioEngineWrap::connectOutput(const QString& left, const QString& right)
{
    if (!m_engine)
        return false;
    return m_engine->connectOutput(left.toStdString(), right.toStdString());
}

void AudioEngineWrap::publishSnapshot(std::unique_ptr<EngineSnapshot> snapshot)
{
    if (m_engine)
        m_engine->publish(std::move(snapshot));
}

void AudioEngineWrap::publishPlaybackSnapshot(std::unique_ptr<EngineSnapshot> snapshot)
{
    if (m_engine)
        m_engine->publishPlayback(std::move(snapshot));
}

void AudioEngineWrap::publishMonitorSnapshot(std::unique_ptr<EngineSnapshot> snapshot)
{
    if (m_engine)
        m_engine->publishMonitor(std::move(snapshot));
}

QVariantMap AudioEngineWrap::takeMonitorLevels()
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

void AudioEngineWrap::clearMonitorClips()
{
    if (m_engine)
        m_engine->clearMonitorClips();
}
