#include "levelmeter.h"

LevelMeter::LevelMeter(std::string label, bool showTarget, double calibrationTargetDb)
    : m_label(std::move(label))
    , m_showTarget(showTarget)
    , m_calibrationTargetDb(calibrationTargetDb)
{
    reset();
}

MeterSnapshot LevelMeter::update(PeakMeter& peakMeter, double elapsedSeconds) noexcept
{
    return updateWithPeak(peakMeter.takePeak(), peakMeter.clipped(), elapsedSeconds);
}

MeterSnapshot LevelMeter::updateWithPeak(float peakGain, bool clipped, double elapsedSeconds) noexcept
{
    m_currentDb = levelscale::decayedPeakDb(m_currentDb, peakGain, elapsedSeconds);
    m_snapshot.db = m_currentDb;
    m_snapshot.normalized = levelscale::normalizedLevel(m_currentDb);
    m_snapshot.levelText = valueformat::meterLevel(m_currentDb);
    m_snapshot.classification = levelscale::classify(m_currentDb);
    m_snapshot.clipped = clipped;
    return m_snapshot;
}

MeterSnapshot LevelMeter::setDirectDb(double db, bool clipped) noexcept
{
    m_currentDb = std::isfinite(db) ? db : levelscale::silenceDb;
    m_snapshot.db = m_currentDb;
    m_snapshot.normalized = levelscale::normalizedLevel(m_currentDb);
    m_snapshot.levelText = valueformat::meterLevel(m_currentDb);
    m_snapshot.classification = levelscale::classify(m_currentDb);
    m_snapshot.clipped = clipped;
    return m_snapshot;
}

void LevelMeter::reset() noexcept
{
    m_currentDb = levelscale::silenceDb;
    m_snapshot.db = levelscale::silenceDb;
    m_snapshot.normalized = 0.0;
    m_snapshot.levelText = valueformat::meterLevel(levelscale::silenceDb);
    m_snapshot.classification = levelscale::Band::Normal;
    m_snapshot.clipped = false;
}
