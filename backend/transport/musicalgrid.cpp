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

int MusicalGrid::lineStride(double unitPixels, double minimumPixels) noexcept
{
    if (!std::isfinite(unitPixels) || unitPixels <= 0 || !std::isfinite(minimumPixels)) return 1;
    const double stride = std::ceil(minimumPixels / unitPixels);
    return stride > 1 && stride < 1e9 ? static_cast<int>(stride) : 1;
}

double MusicalGrid::firstVisibleLine(double scrollPixels, double unitPixels, int stride) noexcept
{
    const double span = unitPixels * std::max(1, stride);
    if (!std::isfinite(scrollPixels) || !std::isfinite(span) || span <= 0) return 0.0;
    return std::floor(scrollPixels / span) * std::max(1, stride);
}

int MusicalGrid::visibleLineCount(double viewPixels, double unitPixels, int stride, int padding) noexcept
{
    const double span = unitPixels * std::max(1, stride);
    if (!std::isfinite(viewPixels) || !std::isfinite(span) || span <= 0 || viewPixels < 0) return 0;
    const double count = std::ceil(viewPixels / span) + padding;
    return count < 1e6 ? static_cast<int>(count) : 1000000;
}

namespace {
bool positive(double value) { return std::isfinite(value) && value > 0; }
int countUp(double value) { return value > 0 && value < 1e6 ? static_cast<int>(std::ceil(value)) : 0; }
}

int MusicalGrid::countedBeatsPerBar(double beatsPerBar, double beatUnit) noexcept
{
    if (!positive(beatsPerBar) || !positive(beatUnit)) return 1;
    return std::max(1, static_cast<int>(std::round(beatsPerBar / beatUnit)));
}

int MusicalGrid::tickCount(double lengthBeats, double beatUnit, bool includeEnd) noexcept
{
    if (!positive(beatUnit) || !std::isfinite(lengthBeats) || lengthBeats < 0) return 0;
    return countUp(lengthBeats / beatUnit) + (includeEnd ? 1 : 0);
}

bool MusicalGrid::isBarTick(int tickIndex, int countedBeatsPerBar) noexcept
{
    return countedBeatsPerBar > 0 && tickIndex >= 0 && tickIndex % countedBeatsPerBar == 0;
}

std::string MusicalGrid::tickLabel(int tickIndex, int countedBeatsPerBar)
{
    const int perBar = std::max(1, countedBeatsPerBar);
    const int index = std::max(0, tickIndex);
    const int bar = index / perBar + 1, beat = index % perBar + 1;
    return beat == 1 ? std::to_string(bar) : std::to_string(bar) + "." + std::to_string(beat);
}

int MusicalGrid::lineCount(double lengthBeats, double beatsPerBar, int linesPerBar) noexcept
{
    if (!positive(beatsPerBar) || linesPerBar <= 0 || !std::isfinite(lengthBeats) || lengthBeats < 0) return 0;
    const double bars = std::ceil(lengthBeats / beatsPerBar);
    const double count = bars * linesPerBar + 1;
    return count < 1e6 ? static_cast<int>(count) : 0;
}

double MusicalGrid::lineOffsetInBar(int lineIndex, int linesPerBar, double spacingBeats) noexcept
{
    if (linesPerBar <= 0 || lineIndex < 0 || !positive(spacingBeats)) return 0.0;
    return (lineIndex % linesPerBar) * spacingBeats;
}

double MusicalGrid::linePosition(int lineIndex, int linesPerBar, double spacingBeats, double beatsPerBar) noexcept
{
    if (linesPerBar <= 0 || lineIndex < 0 || !positive(beatsPerBar)) return 0.0;
    return (lineIndex / linesPerBar) * beatsPerBar + lineOffsetInBar(lineIndex, linesPerBar, spacingBeats);
}

bool MusicalGrid::isBarLineIndex(int lineIndex, int linesPerBar) noexcept
{
    return linesPerBar > 0 && lineIndex >= 0 && lineIndex % linesPerBar == 0;
}

bool MusicalGrid::isOnGrid(double position, double unit, double tolerance) noexcept
{
    if (!std::isfinite(position) || !std::isfinite(unit) || unit <= 0) return false;
    const double ratio = position / unit;
    return std::isfinite(ratio) && std::abs(ratio - std::round(ratio)) < tolerance;
}
