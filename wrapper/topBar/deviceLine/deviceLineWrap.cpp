#include "deviceLineWrap.h"

#include "topBar/deviceLine/deviceline.h"
#include "wrapper/common/engine/engineController.h"

DeviceLineWrap::DeviceLineWrap(QObject* parent) : QObject(parent)
{
    if (auto* engine = EngineController::instance())
        connect(engine, &EngineController::audioInfoChanged, this, &DeviceLineWrap::changed);
}

QString DeviceLineWrap::text() const
{
    const auto* e = EngineController::instance();
    if (!e) return {};
    // The engine runs on JACK only (the status line says so too).
    return QString::fromStdString(deviceline::summary(e->audioRate(), e->bufferFrames(), e->captureFrames(),
                                                      e->playbackFrames(), "JACK"));
}

QString DeviceLineWrap::tooltip() const
{
    const auto* e = EngineController::instance();
    if (!e) return {};
    return QString::fromStdString(deviceline::summaryTooltip(e->audioRate(), e->bufferFrames(), e->captureFrames(),
                                                             e->playbackFrames(), e->roundTripFrames()));
}
