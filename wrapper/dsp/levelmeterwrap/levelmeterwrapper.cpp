#include "levelmeterwrapper.h"

LevelMeterWrapper::LevelMeterWrapper(QObject* parent)
    : QObject(parent)
{
}

QString LevelMeterWrapper::label() const
{
    return QString::fromStdString(m_backend.label());
}

void LevelMeterWrapper::setLabel(const QString& label)
{
    const auto stdLabel = label.toStdString();
    if (m_backend.label() == stdLabel) return;
    m_backend.setLabel(stdLabel);
    emit labelChanged();
}

double LevelMeterWrapper::levelDb() const noexcept
{
    return m_backend.currentSnapshot().db;
}

void LevelMeterWrapper::setLevelDb(double db)
{
    if (m_backend.currentSnapshot().db == db) return;
    m_backend.setDirectDb(db, m_backend.currentSnapshot().clipped);
    emit levelDbChanged();
}

double LevelMeterWrapper::normalized() const noexcept
{
    return m_backend.currentSnapshot().normalized;
}

QString LevelMeterWrapper::levelText() const
{
    return QString::fromStdString(m_backend.currentSnapshot().levelText);
}

int LevelMeterWrapper::classification() const noexcept
{
    return static_cast<int>(m_backend.currentSnapshot().classification);
}

bool LevelMeterWrapper::clipped() const noexcept
{
    return m_backend.currentSnapshot().clipped;
}

void LevelMeterWrapper::setClipped(bool clipped)
{
    if (m_backend.currentSnapshot().clipped == clipped) return;
    m_backend.setDirectDb(m_backend.currentSnapshot().db, clipped);
    emit clippedChanged();
}

bool LevelMeterWrapper::showTarget() const noexcept
{
    return m_backend.showTarget();
}

void LevelMeterWrapper::setShowTarget(bool show)
{
    if (m_backend.showTarget() == show) return;
    m_backend.setShowTarget(show);
    emit showTargetChanged();
}

double LevelMeterWrapper::normalizedTarget() const noexcept
{
    return m_backend.normalizedTarget();
}

void LevelMeterWrapper::reset()
{
    m_backend.reset();
    emit levelDbChanged();
    emit clippedChanged();
}
