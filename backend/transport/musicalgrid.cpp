#include "transport/musicalgrid.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double tolerance = 1e-9;
}

void MusicalGrid::setMeter(double beatsPerBar, double beatUnit) noexcept
{
    if (!std::isfinite(beatsPerBar) || !std::isfinite(beatUnit) || beatsPerBar < 0 || beatUnit <= 0) return;
    m_beatsPerBar = beatsPerBar;
    m_beatUnit = beatUnit;
}

void MusicalGrid::setStep(double stepBeats) noexcept
{
    m_step = std::isfinite(stepBeats) && stepBeats > 0 ? stepBeats : 0.0;
}

double MusicalGrid::snap(double beat, Rounding rounding) const noexcept
{
    if (!std::isfinite(beat)) return 0.0;
    if (m_step <= 0) return std::max(0.0, beat);
    if (m_beatsPerBar <= 0) {
        // No bars: one grid from beat 0
        const double units = beat / m_step;
        if (!std::isfinite(units)) return 0.0;
        const double rounded = rounding == Rounding::Floor ? std::floor(units + tolerance)
            : rounding == Rounding::Ceil ? std::ceil(units - tolerance) : std::round(units);
        return std::max(0.0, rounded * m_step);
    }
    const double bar = std::floor(beat / m_beatsPerBar + tolerance) * m_beatsPerBar;
    if (!std::isfinite(bar)) return 0.0;
    const double offset = beat - bar;
    const double lower = std::floor(offset / m_step + tolerance) * m_step;
    const double upper = std::min(lower + m_step, m_beatsPerBar);
    double snapped = lower;
    switch (rounding) {
    case Rounding::Floor: snapped = lower; break;
    case Rounding::Ceil: snapped = offset - lower > tolerance ? upper : lower; break;
    case Rounding::Nearest: snapped = offset - lower < upper - offset ? lower : upper; break;
    }
    return std::max(0.0, bar + snapped);
}

double MusicalGrid::snapDelta(double deltaBeats) const noexcept
{
    if (!std::isfinite(deltaBeats)) return 0.0;
    if (m_step <= 0) return deltaBeats;
    return std::round(deltaBeats / m_step) * m_step;
}

double MusicalGrid::lineSpacing(double pixelsPerBeat, bool capAtBeat, double minimumPixels) const noexcept
{
    const bool usable = std::isfinite(pixelsPerBeat) && pixelsPerBeat > 0;
    if (!capAtBeat && m_step > 0) {
        double beats = m_step;
        while (usable && beats * pixelsPerBeat < minimumPixels) beats *= 2;
        return beats;
    }
    double beats = m_step > 0 ? std::min(m_beatUnit, m_step) : m_beatUnit;
    while (usable && beats < m_beatUnit && beats * pixelsPerBeat < minimumPixels) beats *= 2;
    return std::min(beats, m_beatUnit);
}

int MusicalGrid::linesPerBar(double spacingBeats) const noexcept
{
    if (!std::isfinite(spacingBeats) || spacingBeats <= 0 || m_beatsPerBar <= 0) return 1;
    return std::max(1, static_cast<int>(std::ceil(m_beatsPerBar / spacingBeats - 1e-6)));
}

bool MusicalGrid::isOnGrid(double position, double unit, double tolerance) noexcept
{
    if (!std::isfinite(position) || !std::isfinite(unit) || unit <= 0) return false;
    const double ratio = position / unit;
    return std::isfinite(ratio) && std::abs(ratio - std::round(ratio)) < tolerance;
}
