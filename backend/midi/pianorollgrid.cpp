#include "midi/pianorollgrid.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double tolerance = 1e-9;
constexpr double minimumLinePixels = 6.0;
constexpr double defaultNoteBeats = 0.25;

template <typename Enum>
bool parse(const std::vector<std::string>& names, const std::string& name, Enum& out)
{
    const auto it = std::find(names.begin(), names.end(), name);
    if (it == names.end()) return false;
    out = static_cast<Enum>(it - names.begin());
    return true;
}
}

const std::vector<std::string>& PianoRollGrid::divisionNames()
{
    static const std::vector<std::string> names{"1/4", "1/8", "1/16", "1/32", "1/64", "Off"};
    return names;
}

const std::vector<std::string>& PianoRollGrid::styleNames()
{
    static const std::vector<std::string> names{"straight", "triplet", "dotted"};
    return names;
}

const std::vector<std::string>& PianoRollGrid::noteLengthNames()
{
    static const std::vector<std::string> names{"Grid", "1/16", "1/8", "1/4"};
    return names;
}

bool PianoRollGrid::setDivision(const std::string& name) { return parse(divisionNames(), name, m_division); }
bool PianoRollGrid::setStyle(const std::string& name) { return parse(styleNames(), name, m_style); }
bool PianoRollGrid::setNoteLength(const std::string& name) { return parse(noteLengthNames(), name, m_noteLength); }

void PianoRollGrid::setMeter(double beatsPerBar, double beatUnit) noexcept
{
    if (!std::isfinite(beatsPerBar) || !std::isfinite(beatUnit) || beatsPerBar <= 0 || beatUnit <= 0) return;
    m_beatsPerBar = beatsPerBar;
    m_beatUnit = beatUnit;
}

std::string PianoRollGrid::divisionName() const { return divisionNames()[static_cast<std::size_t>(m_division)]; }
std::string PianoRollGrid::styleName() const { return styleNames()[static_cast<std::size_t>(m_style)]; }
std::string PianoRollGrid::noteLengthName() const { return noteLengthNames()[static_cast<std::size_t>(m_noteLength)]; }

double PianoRollGrid::divisionBeats() const noexcept
{
    double base = 0.0;
    switch (m_division) {
    case Division::Quarter: base = 1.0; break;
    case Division::Eighth: base = 0.5; break;
    case Division::Sixteenth: base = 0.25; break;
    case Division::ThirtySecond: base = 0.125; break;
    case Division::SixtyFourth: base = 0.0625; break;
    case Division::Off: return 0.0;
    }
    switch (m_style) {
    case Style::Triplet: return base * 2.0 / 3.0;
    case Style::Dotted: return base * 1.5;
    case Style::Straight: break;
    }
    return base;
}

double PianoRollGrid::snapStep() const noexcept
{
    return m_snapEnabled ? divisionBeats() : 0.0;
}

double PianoRollGrid::snapFloor(double beat) const noexcept
{
    if (!std::isfinite(beat)) return 0.0;
    const double step = snapStep();
    if (step <= 0) return std::max(0.0, beat);
    const double bar = std::floor(beat / m_beatsPerBar + tolerance) * m_beatsPerBar;
    return std::max(0.0, bar + std::floor((beat - bar) / step + tolerance) * step);
}

double PianoRollGrid::snapDelta(double deltaBeats) const noexcept
{
    if (!std::isfinite(deltaBeats)) return 0.0;
    const double step = snapStep();
    if (step <= 0) return deltaBeats;
    return std::round(deltaBeats / step) * step;
}

double PianoRollGrid::newNoteLength() const noexcept
{
    switch (m_noteLength) {
    case NoteLength::Sixteenth: return 0.25;
    case NoteLength::Eighth: return 0.5;
    case NoteLength::Quarter: return 1.0;
    case NoteLength::Grid: break;
    }
    const double division = divisionBeats();
    return division > 0 ? division : defaultNoteBeats;
}

double PianoRollGrid::minimumNoteLength() const noexcept
{
    const double step = snapStep();
    return step > 0 ? step : defaultNoteBeats;
}

double PianoRollGrid::quantizeStep() const noexcept
{
    const double division = divisionBeats();
    return division > 0 ? division : m_beatUnit;
}

double PianoRollGrid::lineBeats(double pixelsPerBeat) const noexcept
{
    const bool usable = std::isfinite(pixelsPerBeat) && pixelsPerBeat > 0;
    const double step = divisionBeats();
    if (step > 0 && m_style != Style::Straight) {
        double beats = step;
        while (usable && beats * pixelsPerBeat < minimumLinePixels) beats *= 2;
        return beats;
    }
    double beats = step > 0 ? std::min(m_beatUnit, step) : m_beatUnit;
    while (usable && beats < m_beatUnit && beats * pixelsPerBeat < minimumLinePixels) beats *= 2;
    return std::min(beats, m_beatUnit);
}

int PianoRollGrid::linesPerBar(double pixelsPerBeat) const noexcept
{
    const double beats = lineBeats(pixelsPerBeat);
    return std::max(1, static_cast<int>(std::ceil(m_beatsPerBar / beats - 1e-6)));
}

bool PianoRollGrid::isOnGrid(double position, double unit) noexcept
{
    if (!std::isfinite(position) || !std::isfinite(unit) || unit <= 0) return false;
    const double ratio = position / unit;
    return std::abs(ratio - std::round(ratio)) < 1e-6;
}
