#include "instruments/electrodrums/electrosettings.h"

#include <algorithm>
#include <cmath>

namespace electrodrums {

namespace {

double clampOr(double value, double low, double high, double fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

Settings sanitize(const Settings& in) noexcept
{
    const DrumSettings d;
    Settings s = in;
    if (s.style != Style::Style808 && s.style != Style::Style909 && s.style != Style::Studio)
        s.style = Style::Style808;
    for (auto& drum : s.drums) {
        drum.tuneSemitones = clampOr(drum.tuneSemitones, MinTune, MaxTune, d.tuneSemitones);
        drum.decay = clampOr(drum.decay, MinDecay, MaxDecay, d.decay);
        drum.levelDb = clampOr(drum.levelDb, MinLevelDb, MaxLevelDb, d.levelDb);
    }
    s.volumeDb = clampOr(s.volumeDb, MinLevelDb, MaxLevelDb, 0.0);
    return s;
}

bool operator==(const Settings& a, const Settings& b) noexcept
{
    if (a.style != b.style || a.volumeDb != b.volumeDb) return false;
    for (std::size_t i = 0; i < a.drums.size(); ++i) {
        const auto &x = a.drums[i], &y = b.drums[i];
        if (x.tuneSemitones != y.tuneSemitones || x.decay != y.decay || x.levelDb != y.levelDb) return false;
    }
    return true;
}

bool styleFromId(std::string_view id, Style& style) noexcept
{
    for (const Style s : {Style::Style808, Style::Style909, Style::Studio})
        if (styleId(s) == id) { style = s; return true; }
    return false;
}

bool drumFromId(std::string_view id, Drum& drum) noexcept
{
    for (int i = 0; i < DrumCount; ++i)
        if (drumId(Drum(i)) == id) { drum = Drum(i); return true; }
    return false;
}

std::string_view drumTitle(Drum drum) noexcept
{
    switch (drum) {
    case Drum::Kick: return "Kick";
    case Drum::SideStick: return "Side stick";
    case Drum::Snare: return "Snare";
    case Drum::Clap: return "Clap";
    case Drum::TomLow: return "Tom low";
    case Drum::TomMid: return "Tom mid";
    case Drum::TomHigh: return "Tom high";
    case Drum::HatClosed: return "Hi-hat closed";
    case Drum::HatPedal: return "Hi-hat pedal";
    case Drum::HatOpen: return "Hi-hat open";
    case Drum::Crash: return "Crash";
    case Drum::Ride: return "Ride";
    case Drum::Count: break;
    }
    return {};
}

} // namespace electrodrums
