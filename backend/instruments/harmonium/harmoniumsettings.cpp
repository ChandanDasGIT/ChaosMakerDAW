#include "instruments/harmonium/harmoniumsettings.h"

#include <algorithm>
#include <cmath>

namespace harmonium {

namespace {

double clampOr(double value, double low, double high, double fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

Settings sanitize(const Settings& in) noexcept
{
    const Settings defaults;
    Settings s = in;
    if (s.style != Style::Indian && s.style != Style::Western) s.style = Style::Indian;
    s.bellows = clampOr(s.bellows, 0.0, 1.0, defaults.bellows);
    s.pumping = clampOr(s.pumping, 0.0, 1.0, defaults.pumping);
    s.beatingCents = clampOr(s.beatingCents, 0.0, MaxBeatingCents, defaults.beatingCents);
    s.airNoise = clampOr(s.airNoise, 0.0, 1.0, defaults.airNoise);
    s.keyClick = clampOr(s.keyClick, 0.0, 1.0, defaults.keyClick);
    s.caseResonance = clampOr(s.caseResonance, 0.0, 1.0, defaults.caseResonance);
    s.sustainSeconds = clampOr(s.sustainSeconds, 0.0, MaxSustainSeconds, defaults.sustainSeconds);
    s.transpose = std::clamp(s.transpose, -MaxTranspose, MaxTranspose);
    s.volumeDb = clampOr(s.volumeDb, MinVolumeDb, MaxVolumeDb, defaults.volumeDb);
    for (auto& key : s.drones)
        if (key < 0 || key > 127) key = NoDrone;
    return s;
}

bool operator==(const IndianStops& a, const IndianStops& b) noexcept
{
    return a.bass == b.bass && a.male == b.male && a.female == b.female;
}

bool operator==(const WesternStops& a, const WesternStops& b) noexcept
{
    return a.bourdon16 == b.bourdon16 && a.diapason8 == b.diapason8 && a.dulciana8 == b.dulciana8
        && a.principal4 == b.principal4 && a.voxHumana == b.voxHumana;
}

bool operator==(const Settings& a, const Settings& b) noexcept
{
    return a.style == b.style && a.indian == b.indian && a.western == b.western
        && a.bellows == b.bellows && a.pumping == b.pumping && a.beatingCents == b.beatingCents
        && a.airNoise == b.airNoise && a.keyClick == b.keyClick && a.caseResonance == b.caseResonance
        && a.sustainSeconds == b.sustainSeconds
        && a.transpose == b.transpose && a.volumeDb == b.volumeDb && a.drones == b.drones;
}

std::string_view styleName(Style style) noexcept
{
    return style == Style::Western ? "western" : "indian";
}

bool styleFromName(std::string_view name, Style& style) noexcept
{
    if (name == "indian") { style = Style::Indian; return true; }
    if (name == "western") { style = Style::Western; return true; }
    return false;
}

} // namespace harmonium
