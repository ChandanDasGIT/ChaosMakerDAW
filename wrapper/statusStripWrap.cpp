#include "statusStripWrap.h"

#include "common/engine/enginestatus.h"
#include "statusstrip.h"
#include "wrapper/common/engine/engineController.h"

#include <algorithm>
#include <limits>

StatusStripWrap::StatusStripWrap(QObject* parent) : QObject(parent)
{
    if (auto* engine = EngineController::instance()) {
        connect(engine, &EngineController::audioInfoChanged, this, &StatusStripWrap::changed);
        connect(engine, &EngineController::transportMessageChanged, this, &StatusStripWrap::changed);
        connect(engine, &EngineController::statusChanged, this, &StatusStripWrap::changed);
    }
}

QString StatusStripWrap::message() const
{
    const auto* e = EngineController::instance();
    if (!e) return {};
    return QString::fromStdString(statusstrip::message(e->transportMessage().toStdString(),
                                                       e->version().toStdString(), e->status().toStdString()));
}

QString StatusStripWrap::sampleRateText() const
{
    const auto* e = EngineController::instance();
    return e ? QString::fromStdString(enginestatus::sampleRate(e->audioRate())) : QString();
}

QString StatusStripWrap::bufferText() const
{
    const auto* e = EngineController::instance();
    return e ? QString::fromStdString(statusstrip::buffer(e->bufferFrames(), e->audioRate())) : QString();
}

QString StatusStripWrap::bufferTooltip() const
{
    const auto* e = EngineController::instance();
    return e ? QString::fromStdString(statusstrip::bufferTooltip(e->bufferFrames(), e->requestedBufferFrames(), e->audioRate()))
             : QString();
}

int StatusStripWrap::xrunCount() const
{
    const auto* e = EngineController::instance();
    return e ? static_cast<int>(std::min<std::uint64_t>(e->xruns(), std::numeric_limits<int>::max())) : 0;
}

QString StatusStripWrap::xrunText() const
{
    const auto* e = EngineController::instance();
    return e ? QString::fromStdString(statusstrip::xruns(e->xruns())) : QString();
}

QString StatusStripWrap::xrunTooltip() const
{
    const auto* e = EngineController::instance();
    return e ? QString::fromStdString(statusstrip::xrunTooltip(e->xruns())) : QString();
}

void StatusStripWrap::resetXruns()
{
    if (auto* e = EngineController::instance()) e->resetXruns();
}
