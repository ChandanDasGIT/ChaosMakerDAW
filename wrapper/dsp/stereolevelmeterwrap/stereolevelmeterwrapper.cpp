#include "stereolevelmeterwrapper.h"

StereoLevelMeterWrapper::StereoLevelMeterWrapper(QObject* parent)
    : QObject(parent)
{
}

QString StereoLevelMeterWrapper::label() const
{
    return QString::fromStdString(m_backend.label());
}

void StereoLevelMeterWrapper::setLabel(const QString& label)
{
    const auto stdLabel = label.toStdString();
    if (m_backend.label() == stdLabel) return;
    m_backend.setLabel(stdLabel);
    emit labelChanged();
    emit tooltipTextChanged();
}

double StereoLevelMeterWrapper::leftDb() const noexcept
{
    return m_backend.currentSnapshot().db[0];
}

void StereoLevelMeterWrapper::setLeftDb(double db)
{
    if (m_backend.currentSnapshot().db[0] == db) return;
    const auto snap = m_backend.currentSnapshot();
    m_backend.setDirectDb(db, snap.db[1], snap.clipped[0], snap.clipped[1]);
    emit levelsChanged();
    emit tooltipTextChanged();
}

double StereoLevelMeterWrapper::rightDb() const noexcept
{
    return m_backend.currentSnapshot().db[1];
}

void StereoLevelMeterWrapper::setRightDb(double db)
{
    if (m_backend.currentSnapshot().db[1] == db) return;
    const auto snap = m_backend.currentSnapshot();
    m_backend.setDirectDb(snap.db[0], db, snap.clipped[0], snap.clipped[1]);
    emit levelsChanged();
    emit tooltipTextChanged();
}

double StereoLevelMeterWrapper::leftNormalized() const noexcept
{
    return m_backend.currentSnapshot().normalized[0];
}

double StereoLevelMeterWrapper::rightNormalized() const noexcept
{
    return m_backend.currentSnapshot().normalized[1];
}

QString StereoLevelMeterWrapper::leftLevelText() const
{
    return QString::fromStdString(m_backend.currentSnapshot().levelText[0]);
}

QString StereoLevelMeterWrapper::rightLevelText() const
{
    return QString::fromStdString(m_backend.currentSnapshot().levelText[1]);
}

int StereoLevelMeterWrapper::leftClassification() const noexcept
{
    return static_cast<int>(m_backend.currentSnapshot().classification[0]);
}

int StereoLevelMeterWrapper::rightClassification() const noexcept
{
    return static_cast<int>(m_backend.currentSnapshot().classification[1]);
}

bool StereoLevelMeterWrapper::leftClipped() const noexcept
{
    return m_backend.currentSnapshot().clipped[0];
}

void StereoLevelMeterWrapper::setLeftClipped(bool clipped)
{
    if (m_backend.currentSnapshot().clipped[0] == clipped) return;
    const auto snap = m_backend.currentSnapshot();
    m_backend.setDirectDb(snap.db[0], snap.db[1], clipped, snap.clipped[1]);
    emit clipChanged();
}

bool StereoLevelMeterWrapper::rightClipped() const noexcept
{
    return m_backend.currentSnapshot().clipped[1];
}

void StereoLevelMeterWrapper::setRightClipped(bool clipped)
{
    if (m_backend.currentSnapshot().clipped[1] == clipped) return;
    const auto snap = m_backend.currentSnapshot();
    m_backend.setDirectDb(snap.db[0], snap.db[1], snap.clipped[0], clipped);
    emit clipChanged();
}

QString StereoLevelMeterWrapper::tooltipText() const
{
    return QString::fromStdString(m_backend.currentSnapshot().tooltipText);
}

void StereoLevelMeterWrapper::reset()
{
    m_backend.reset();
    emit levelsChanged();
    emit clipChanged();
    emit tooltipTextChanged();
}

void StereoLevelMeterWrapper::requestClearClips()
{
    emit clearClipsRequested();
}
