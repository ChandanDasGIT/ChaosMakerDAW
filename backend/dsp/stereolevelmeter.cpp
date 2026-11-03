#include "stereolevelmeter.h"

StereoLevelMeter::StereoLevelMeter(std::string label)
    : m_label(std::move(label))
{
    reset();
}

void StereoLevelMeter::setLabel(std::string label)
{
    m_label = std::move(label);
    m_snapshot.tooltipText = formatTooltip(m_label, m_snapshot.db[0], m_snapshot.db[1]);
}

StereoMeterSnapshot StereoLevelMeter::update(PeakMeter& left, PeakMeter& right, double elapsedSeconds) noexcept
{
    return updateWithPeaks(left.takePeak(), left.clipped(), right.takePeak(), right.clipped(), elapsedSeconds);
}

StereoMeterSnapshot StereoLevelMeter::updateWithPeaks(float leftPeak, bool leftClipped, float rightPeak, bool rightClipped, double elapsedSeconds) noexcept
{
    m_currentDb[0] = levelscale::decayedPeakDb(m_currentDb[0], leftPeak, elapsedSeconds);
    m_currentDb[1] = levelscale::decayedPeakDb(m_currentDb[1], rightPeak, elapsedSeconds);

    m_snapshot.db = m_currentDb;
    m_snapshot.normalized[0] = levelscale::normalizedLevel(m_currentDb[0]);
    m_snapshot.normalized[1] = levelscale::normalizedLevel(m_currentDb[1]);
    m_snapshot.levelText[0] = valueformat::meterLevel(m_currentDb[0]);
    m_snapshot.levelText[1] = valueformat::meterLevel(m_currentDb[1]);
    m_snapshot.classification[0] = levelscale::classify(m_currentDb[0]);
    m_snapshot.classification[1] = levelscale::classify(m_currentDb[1]);
    m_snapshot.clipped[0] = leftClipped;
    m_snapshot.clipped[1] = rightClipped;
    m_snapshot.tooltipText = formatTooltip(m_label, m_currentDb[0], m_currentDb[1]);

    return m_snapshot;
}

StereoMeterSnapshot StereoLevelMeter::setDirectDb(double leftDb, double rightDb, bool leftClipped, bool rightClipped) noexcept
{
    m_currentDb[0] = std::isfinite(leftDb) ? leftDb : levelscale::silenceDb;
    m_currentDb[1] = std::isfinite(rightDb) ? rightDb : levelscale::silenceDb;

    m_snapshot.db = m_currentDb;
    m_snapshot.normalized[0] = levelscale::normalizedLevel(m_currentDb[0]);
    m_snapshot.normalized[1] = levelscale::normalizedLevel(m_currentDb[1]);
    m_snapshot.levelText[0] = valueformat::meterLevel(m_currentDb[0]);
    m_snapshot.levelText[1] = valueformat::meterLevel(m_currentDb[1]);
    m_snapshot.classification[0] = levelscale::classify(m_currentDb[0]);
    m_snapshot.classification[1] = levelscale::classify(m_currentDb[1]);
    m_snapshot.clipped[0] = leftClipped;
    m_snapshot.clipped[1] = rightClipped;
    m_snapshot.tooltipText = formatTooltip(m_label, m_currentDb[0], m_currentDb[1]);

    return m_snapshot;
}

void StereoLevelMeter::reset() noexcept
{
    m_currentDb = {levelscale::silenceDb, levelscale::silenceDb};
    m_snapshot.db = m_currentDb;
    m_snapshot.normalized = {0.0, 0.0};
    m_snapshot.levelText = {valueformat::meterLevel(levelscale::silenceDb), valueformat::meterLevel(levelscale::silenceDb)};
    m_snapshot.classification = {levelscale::Band::Normal, levelscale::Band::Normal};
    m_snapshot.clipped = {false, false};
    m_snapshot.tooltipText = formatTooltip(m_label, m_currentDb[0], m_currentDb[1]);
}

std::string StereoLevelMeter::formatTooltip(std::string_view label, double leftDb, double rightDb)
{
    std::string text;
    if (!label.empty()) {
        text.append(label);
        text.append(" ");
    }
    text.append("L: ");
    text.append(valueformat::meterLevel(leftDb));
    text.append(" dBFS / R: ");
    text.append(valueformat::meterLevel(rightDb));
    text.append(" dBFS\nClick to clear clip indicators");
    return text;
}
