#include "chaoseqcurve.h"

#include <algorithm>
#include <cmath>

namespace chaoseq {

double curveFrequency(int index, int count) noexcept
{
    if (count <= 1) return CurveMinHz;
    const double t = std::clamp(double(index) / double(count - 1), 0.0, 1.0);
    return CurveMinHz * std::pow(CurveMaxHz / CurveMinHz, t);
}

Curve curve(const Settings& input, double sampleRate, int points)
{
    const double rate = std::isfinite(sampleRate) && sampleRate > 1000.0 ? sampleRate : 48000.0;
    const Settings settings = sanitize(input, rate);
    const auto count = static_cast<std::size_t>(std::clamp(points, 2, 4096));
    Curve result;
    result.frequencies.resize(count);
    result.totalDb.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i)
        result.frequencies[i] = curveFrequency(int(i), int(count));

    for (int b = 0; b < MaxBands; ++b) {
        const auto& band = settings.bands[static_cast<std::size_t>(b)];
        if (!band.inUse) continue;
        const auto design = eqband::design(band.filter, rate);
        auto& values = result.bandDb[static_cast<std::size_t>(b)];
        values.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            // Above Nyquist the band has no effect; hold the last real value.
            const double hz = std::min(result.frequencies[i], rate * 0.499);
            values[i] = eqband::magnitudeDb(design, hz, rate);
            if (!band.bypassed) result.totalDb[i] += values[i];
        }
    }
    return result;
}

} // namespace chaoseq
