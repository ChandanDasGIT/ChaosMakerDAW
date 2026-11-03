#include "playbackcontrolswrapper.h"

PlaybackControlsWrapper::PlaybackControlsWrapper(QObject* parent)
    : QObject(parent)
    , m_controls(m_ownedControls)
{
}

PlaybackControlsWrapper::PlaybackControlsWrapper(PlaybackControls* backend, QObject* parent)
    : QObject(parent)
    , m_controls(backend ? *backend : m_ownedControls)
{
}

void PlaybackControlsWrapper::setPlaying(bool playing)
{
    if (m_controls.isPlaying() != playing) {
        m_controls.setState(playing ? PlaybackControls::State::Playing : PlaybackControls::State::Stopped);
        emit transportChanged();
    }
}

void PlaybackControlsWrapper::setRecording(bool recording)
{
    if (m_controls.isRecording() != recording) {
        m_controls.setState(recording ? PlaybackControls::State::Recording
                                     : (m_controls.isPlaying() ? PlaybackControls::State::Playing : PlaybackControls::State::Stopped));
        emit transportChanged();
    }
}

void PlaybackControlsWrapper::setRepeating(bool repeating)
{
    if (m_controls.isRepeating() != repeating) {
        m_controls.setRepeating(repeating);
        emit transportChanged();
    }
}

void PlaybackControlsWrapper::setCountingIn(bool countingIn)
{
    if (m_controls.isCountingIn() != countingIn) {
        m_controls.setCountingIn(countingIn);
        emit transportChanged();
    }
}

void PlaybackControlsWrapper::updatePosition(double seconds, double beats)
{
    if (m_positionSeconds != seconds || m_positionBeats != beats) {
        m_positionSeconds = seconds;
        m_positionBeats = beats;
        emit positionChanged();
    }
}

QString PlaybackControlsWrapper::formattedTime() const
{
    return QString::fromStdString(PlaybackControls::formatTime(m_positionSeconds));
}

QString PlaybackControlsWrapper::formattedBarsBeats() const
{
    return QString::fromStdString(PlaybackControls::formatBarsBeats(m_positionBeats, m_numerator, m_denominator));
}

void PlaybackControlsWrapper::setTransportMessage(const QString& msg)
{
    if (m_transportMessage != msg) {
        m_transportMessage = msg;
        emit transportMessageChanged();
    }
}

void PlaybackControlsWrapper::transportAction(const QString& action)
{
    emit actionRequested(action);
}

void PlaybackControlsWrapper::seekBeat(double beat)
{
    emit seekRequested(beat);
}

bool PlaybackControlsWrapper::setRepeatRange(double startBeat, double endBeat)
{
    if (m_controls.setRepeatRange(startBeat, endBeat, 48000.0, 120.0)) {
        emit repeatRangeRequested(startBeat, endBeat);
        return true;
    }
    return false;
}
