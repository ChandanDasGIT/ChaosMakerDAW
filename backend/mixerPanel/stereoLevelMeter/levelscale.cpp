#include "levelscale.h"

#include <algorithm>
#include <cmath>
#include <limits>

double levelscale::gainToDb(double gain) noexcept
{
    if (!std::isfinite(gain) || gain <= 0) return silenceDb;
    return std::max(silenceDb, 20.0 * std::log10(gain));
}

double levelscale::dbToGain(double db) noexcept
{
    if (!std::isfinite(db)) return 0;
    const auto gain = std::pow(10.0, db / 20.0);
    return std::isfinite(gain) ? gain : std::numeric_limits<double>::max();
}

double levelscale::normalizedLevel(double db) noexcept
{
    if (!std::isfinite(db)) return 0;
    return std::clamp((db - meterFloorDb) / -meterFloorDb, 0.0, 1.0);
}

levelscale::Band levelscale::classify(double db) noexcept
{
    if (!std::isfinite(db)) return Band::Normal;
    if (db >= 0) return Band::Clip;
    return db > warningDb ? Band::Warning : Band::Normal;
}

double levelscale::decayedPeakDb(double previousDb, double peakGain, double elapsedSeconds) noexcept
{
    if (!std::isfinite(previousDb)) previousDb = silenceDb;
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0) elapsedSeconds = 0;
    return std::max(gainToDb(peakGain), previousDb - decayDbPerSecond * elapsedSeconds);
}
