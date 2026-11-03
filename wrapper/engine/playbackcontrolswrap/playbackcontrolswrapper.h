#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include "engine/playbackcontrols.h"

class PlaybackControlsWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool playing READ isPlaying NOTIFY transportChanged)
    Q_PROPERTY(bool recording READ isRecording NOTIFY transportChanged)
    Q_PROPERTY(bool repeating READ isRepeating NOTIFY transportChanged)
    Q_PROPERTY(bool countingIn READ isCountingIn NOTIFY transportChanged)
    Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY positionChanged)
    Q_PROPERTY(double positionBeats READ positionBeats NOTIFY positionChanged)
    Q_PROPERTY(QString formattedTime READ formattedTime NOTIFY positionChanged)
    Q_PROPERTY(QString formattedBarsBeats READ formattedBarsBeats NOTIFY positionChanged)
    Q_PROPERTY(QString transportMessage READ transportMessage NOTIFY transportMessageChanged)

public:
    explicit PlaybackControlsWrapper(QObject* parent = nullptr);
    explicit PlaybackControlsWrapper(PlaybackControls* backend, QObject* parent = nullptr);

    PlaybackControls& controls() noexcept { return m_controls; }
    const PlaybackControls& controls() const noexcept { return m_controls; }

    bool isPlaying() const noexcept { return m_controls.isPlaying(); }
    bool isRecording() const noexcept { return m_controls.isRecording(); }
    bool isRepeating() const noexcept { return m_controls.isRepeating(); }
    bool isCountingIn() const noexcept { return m_controls.isCountingIn(); }

    void setPlaying(bool playing);
    void setRecording(bool recording);
    void setRepeating(bool repeating);
    void setCountingIn(bool countingIn);

    double positionSeconds() const noexcept { return m_positionSeconds; }
    double positionBeats() const noexcept { return m_positionBeats; }
    void updatePosition(double seconds, double beats);

    QString formattedTime() const;
    QString formattedBarsBeats() const;

    QString transportMessage() const { return m_transportMessage; }
    void setTransportMessage(const QString& msg);

    Q_INVOKABLE void transportAction(const QString& action);
    Q_INVOKABLE void seekBeat(double beat);
    Q_INVOKABLE bool setRepeatRange(double startBeat, double endBeat);

    Q_INVOKABLE void play() { transportAction(QStringLiteral("play")); }
    Q_INVOKABLE void pause() { transportAction(QStringLiteral("pause")); }
    Q_INVOKABLE void stop() { transportAction(QStringLiteral("stop")); }
    Q_INVOKABLE void record() { transportAction(QStringLiteral("record")); }
    Q_INVOKABLE void rewind() { transportAction(QStringLiteral("rewind")); }
    Q_INVOKABLE void forward() { transportAction(QStringLiteral("forward")); }
    Q_INVOKABLE void toggleRepeat() { transportAction(QStringLiteral("repeat")); }

signals:
    void transportChanged();
    void positionChanged();
    void transportMessageChanged();
    void actionRequested(const QString& action);
    void seekRequested(double beat);
    void repeatRangeRequested(double startBeat, double endBeat);

private:
    PlaybackControls m_ownedControls;
    PlaybackControls& m_controls;
    double m_positionSeconds = 0.0;
    double m_positionBeats = 0.0;
    int m_numerator = 4;
    int m_denominator = 4;
    QString m_transportMessage;
};
