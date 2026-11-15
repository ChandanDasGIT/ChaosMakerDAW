#include "levelMeterWrap.h"

LevelMeterWrap::LevelMeterWrap(QObject* parent)
    : QObject(parent)
{
}

QString LevelMeterWrap::label() const
{
    return QString::fromStdString(m_backend.label());
}

void LevelMeterWrap::setLabel(const QString& label)
{
    const auto stdLabel = label.toStdString();
    if (m_backend.label() == stdLabel) return;
    m_backend.setLabel(stdLabel);
    emit labelChanged();
}

double LevelMeterWrap::levelDb() const noexcept
{
    return m_backend.currentSnapshot().db;
}

void LevelMeterWrap::setLevelDb(double db)
{
    if (m_backend.currentSnapshot().db == db) return;
    m_backend.setDirectDb(db, m_backend.currentSnapshot().clipped);
    emit levelDbChanged();
}

double LevelMeterWrap::normalized() const noexcept
{
    return m_backend.currentSnapshot().normalized;
}

QString LevelMeterWrap::levelText() const
{
    return QString::fromStdString(m_backend.currentSnapshot().levelText);
}

int LevelMeterWrap::classification() const noexcept
{
    return static_cast<int>(m_backend.currentSnapshot().classification);
}

bool LevelMeterWrap::clipped() const noexcept
{
    return m_backend.currentSnapshot().clipped;
}

void LevelMeterWrap::setClipped(bool clipped)
{
    if (m_backend.currentSnapshot().clipped == clipped) return;
    m_backend.setDirectDb(m_backend.currentSnapshot().db, clipped);
    emit clippedChanged();
}

bool LevelMeterWrap::showTarget() const noexcept
{
    return m_backend.showTarget();
}

void LevelMeterWrap::setShowTarget(bool show)
{
    if (m_backend.showTarget() == show) return;
    m_backend.setShowTarget(show);
    emit showTargetChanged();
}

double LevelMeterWrap::normalizedTarget() const noexcept
{
    return m_backend.normalizedTarget();
}

void LevelMeterWrap::reset()
{
    m_backend.reset();
    emit levelDbChanged();
    emit clippedChanged();
}
