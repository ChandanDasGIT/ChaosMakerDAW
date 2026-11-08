#include "instruments/piano/pianosettings.h"

#include <algorithm>
#include <cmath>

namespace piano {

namespace {

double clampOr(double value, double low, double high, double fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

Settings sanitize(const Settings& in) noexcept
{
    const Settings d;
    Settings s = in;
    s.volumeDb = clampOr(s.volumeDb, MinVolumeDb, MaxVolumeDb, d.volumeDb);
    s.release = clampOr(s.release, 0.0, 1.0, d.release);
    s.resonance = clampOr(s.resonance, 0.0, 1.0, d.resonance);
    s.velocity = clampOr(s.velocity, 0.0, 1.0, d.velocity);
    s.brightness = clampOr(s.brightness, 0.0, 1.0, d.brightness);
    s.transpose = std::clamp(s.transpose, -MaxTranspose, MaxTranspose);
    return s;
}

bool operator==(const Settings& a, const Settings& b) noexcept
{
    return a.volumeDb == b.volumeDb && a.release == b.release && a.resonance == b.resonance
        && a.velocity == b.velocity && a.brightness == b.brightness && a.transpose == b.transpose;
}

std::map<int, double> controllers(const Settings& in)
{
    const Settings s = sanitize(in);
    return {{72, s.release * 127.0}, {70, s.resonance * 127.0}, {99, s.velocity * 127.0}};
}

double brightnessCutoff(double brightness) noexcept
{
    if (!std::isfinite(brightness) || brightness >= 1.0) return 0.0;
    return 1000.0 * std::pow(20.0, std::max(brightness, 0.0));
}

} // namespace piano
