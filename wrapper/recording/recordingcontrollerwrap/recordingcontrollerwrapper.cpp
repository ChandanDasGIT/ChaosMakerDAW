#include "recordingcontrollerwrapper.h"

RecordingControllerWrapper::RecordingControllerWrapper(QObject* parent)
    : QObject(parent)
    , m_ownedController()
    , m_controller(m_ownedController)
{
}

RecordingControllerWrapper::RecordingControllerWrapper(RecordingController* controller, QObject* parent)
    : QObject(parent)
    , m_ownedController()
    , m_controller(controller ? *controller : m_ownedController)
{
}

QVariantList RecordingControllerWrapper::recordingTracks() const
{
    QVariantList list;
    for (int id : m_controller.activeRecordingTrackIds()) {
        list.push_back(id);
    }
    return list;
}

void RecordingControllerWrapper::notifyStateChanged()
{
    emit stateChanged();
}
