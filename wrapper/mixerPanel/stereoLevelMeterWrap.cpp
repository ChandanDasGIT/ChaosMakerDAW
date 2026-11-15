#include "stereoLevelMeterWrap.h"
#include "mixerPanel/stereoLevelMeter/levelscale.h"

double StereoLevelMeterWrap::normalizedFor(double db) const noexcept
{
    return levelscale::normalizedLevel(db);
}

StereoLevelMeterWrap::StereoLevelMeterWrap(QObject* parent)
    : QObject(parent)
{
}

QString StereoLevelMeterWrap::label() const
{
    return QString::fromStdString(m_backend.label());
}

void StereoLevelMeterWrap::setLabel(const QString& label)
{
    const auto stdLabel = label.toStdString();
    if (m_backend.label() == stdLabel) return;
    m_backend.setLabel(stdLabel);
    emit labelChanged();
    emit tooltipTextChanged();
}

double StereoLevelMeterWrap::leftDb() const noexcept
{
    return m_backend.currentSnapshot().db[0];
}

void StereoLevelMeterWrap::setLeftDb(double db)
{
    if (m_backend.currentSnapshot().db[0] == db) return;
    const auto snap = m_backend.currentSnapshot();
    m_backend.setDirectDb(db, snap.db[1], snap.clipped[0], snap.clipped[1]);
    emit levelsChanged();
    emit tooltipTextChanged();
}

double StereoLevelMeterWrap::rightDb() const noexcept
{
    return m_backend.currentSnapshot().db[1];
}

void StereoLevelMeterWrap::setRightDb(double db)
{
    if (m_backend.currentSnapshot().db[1] == db) return;
    const auto snap = m_backend.currentSnapshot();
    m_backend.setDirectDb(snap.db[0], db, snap.clipped[0], snap.clipped[1]);
    emit levelsChanged();
    emit tooltipTextChanged();
}

double StereoLevelMeterWrap::leftNormalized() const noexcept
{
    return m_backend.currentSnapshot().normalized[0];
}

double StereoLevelMeterWrap::rightNormalized() const noexcept
{
    return m_backend.currentSnapshot().normalized[1];
}

QString StereoLevelMeterWrap::leftLevelText() const
{
    return QString::fromStdString(m_backend.currentSnapshot().levelText[0]);
}

QString StereoLevelMeterWrap::rightLevelText() const
{
    return QString::fromStdString(m_backend.currentSnapshot().levelText[1]);
}

int StereoLevelMeterWrap::leftClassification() const noexcept
{
    return static_cast<int>(m_backend.currentSnapshot().classification[0]);
}

int StereoLevelMeterWrap::rightClassification() const noexcept
{
    return static_cast<int>(m_backend.currentSnapshot().classification[1]);
}

bool StereoLevelMeterWrap::leftClipped() const noexcept
{
    return m_backend.currentSnapshot().clipped[0];
}

void StereoLevelMeterWrap::setLeftClipped(bool clipped)
{
    if (m_backend.currentSnapshot().clipped[0] == clipped) return;
    const auto snap = m_backend.currentSnapshot();
    m_backend.setDirectDb(snap.db[0], snap.db[1], clipped, snap.clipped[1]);
    emit clipChanged();
}

bool StereoLevelMeterWrap::rightClipped() const noexcept
{
    return m_backend.currentSnapshot().clipped[1];
}

void StereoLevelMeterWrap::setRightClipped(bool clipped)
{
    if (m_backend.currentSnapshot().clipped[1] == clipped) return;
    const auto snap = m_backend.currentSnapshot();
    m_backend.setDirectDb(snap.db[0], snap.db[1], snap.clipped[0], clipped);
    emit clipChanged();
}

QString StereoLevelMeterWrap::tooltipText() const
{
    return QString::fromStdString(m_backend.currentSnapshot().tooltipText);
}

void StereoLevelMeterWrap::reset()
{
    m_backend.reset();
    emit levelsChanged();
    emit clipChanged();
    emit tooltipTextChanged();
}

void StereoLevelMeterWrap::requestClearClips()
{
    emit clearClipsRequested();
}
