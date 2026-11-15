#include "common/engine/enginestatus.h"
#include "common/core/valueformat.h"

#include <cmath>

namespace {
const std::string dash = "\xe2\x80\x94"; // "—"

// Whole kHz without decimals, otherwise one decimal: 48 -> "48", 44.1 -> "44.1".
std::string kilohertz(double hz)
{
    const double khz = std::round(hz / 100.0) / 10.0;
    return valueformat::fixed(khz, khz == std::floor(khz) ? 0 : 1);
}
}

bool enginestatus::validRate(double hz) { return std::isfinite(hz) && hz > 0.0; }

std::string enginestatus::sampleRate(double hz)
{
    if (!validRate(hz)) return dash;
    return kilohertz(hz) + " kHz";
}
