#include "playbackControlsWrap.h"

PlaybackControlsWrap::PlaybackControlsWrap(QObject* parent)
    : QObject(parent)
    , m_controls(m_ownedControls)
{
}

PlaybackControlsWrap::PlaybackControlsWrap(PlaybackControls* backend, QObject* parent)
    : QObject(parent)
    , m_controls(backend ? *backend : m_ownedControls)
{
}

void PlaybackControlsWrap::setPlaying(bool playing)
{
    if (m_controls.isPlaying() != playing) {
        m_controls.setState(playing ? PlaybackControls::State::Playing : PlaybackControls::State::Stopped);
        emit transportChanged();
    }
}

void PlaybackControlsWrap::setRecording(bool recording)
{
    if (m_controls.isRecording() != recording) {
        m_controls.setState(recording ? PlaybackControls::State::Recording
                                     : (m_controls.isPlaying() ? PlaybackControls::State::Playing : PlaybackControls::State::Stopped));
        emit transportChanged();
    }
}

void PlaybackControlsWrap::setRepeating(bool repeating)
{
    if (m_controls.isRepeating() != repeating) {
        m_controls.setRepeating(repeating);
        emit transportChanged();
    }
}

void PlaybackControlsWrap::setCountingIn(bool countingIn)
{
    if (m_controls.isCountingIn() != countingIn) {
        m_controls.setCountingIn(countingIn);
        emit transportChanged();
    }
}

void PlaybackControlsWrap::updatePosition(double seconds, double beats)
{
    if (m_positionSeconds != seconds || m_positionBeats != beats) {
        m_positionSeconds = seconds;
        m_positionBeats = beats;
        emit positionChanged();
    }
}

QString PlaybackControlsWrap::formattedTime() const
{
    return QString::fromStdString(PlaybackControls::formatTime(m_positionSeconds));
}

QString PlaybackControlsWrap::formattedBarsBeats() const
{
    return QString::fromStdString(PlaybackControls::formatBarsBeats(m_positionBeats, m_numerator, m_denominator));
}

void PlaybackControlsWrap::setTransportMessage(const QString& msg)
{
    if (m_transportMessage != msg) {
        m_transportMessage = msg;
        emit transportMessageChanged();
    }
}

void PlaybackControlsWrap::transportAction(const QString& action)
{
    emit actionRequested(action);
}

void PlaybackControlsWrap::seekBeat(double beat)
{
    emit seekRequested(beat);
}

bool PlaybackControlsWrap::setRepeatRange(double startBeat, double endBeat)
{
    if (m_controls.setRepeatRange(startBeat, endBeat, 48000.0, 120.0)) {
        emit repeatRangeRequested(startBeat, endBeat);
        return true;
    }
    return false;
}
