#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include "recording/recordingcontroller.h"

class RecordingControllerWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool takeBusy READ takeBusy NOTIFY stateChanged)
    Q_PROPERTY(int recordingTrackId READ recordingTrackId NOTIFY stateChanged)
    Q_PROPERTY(QVariantList recordingTracks READ recordingTracks NOTIFY stateChanged)
    Q_PROPERTY(double recordingStartBeats READ recordingStartBeats NOTIFY stateChanged)

public:
    explicit RecordingControllerWrapper(QObject* parent = nullptr);
    explicit RecordingControllerWrapper(RecordingController* controller, QObject* parent = nullptr);

    RecordingController& controller() { return m_controller; }
    const RecordingController& controller() const { return m_controller; }

    bool takeBusy() const { return m_controller.isBusy(); }
    int recordingTrackId() const { return m_controller.primaryRecordingTrackId(); }
    QVariantList recordingTracks() const;
    double recordingStartBeats() const { return m_controller.recordStartBeat(); }

    void notifyStateChanged();

signals:
    void stateChanged();
    void takeCompleted(const QString& message);

private:
    RecordingController m_ownedController;
    RecordingController& m_controller;
};
