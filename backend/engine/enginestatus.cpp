#include "engine/enginestatus.h"
#include "core/valueformat.h"

#include <algorithm>
#include <cmath>

namespace {
const std::string dash = "\xe2\x80\x94"; // "—"

bool validRate(double hz) { return std::isfinite(hz) && hz > 0.0; }

// Whole kHz without decimals, otherwise one decimal: 48 -> "48", 44.1 -> "44.1".
std::string kilohertz(double hz)
{
    const double khz = std::round(hz / 100.0) / 10.0;
    return valueformat::fixed(khz, khz == std::floor(khz) ? 0 : 1);
}
}

std::string enginestatus::sampleRate(double hz)
{
    if (!validRate(hz)) return dash;
    return kilohertz(hz) + " kHz";
}

std::string enginestatus::buffer(int frames, double hz)
{
    if (frames <= 0 || !validRate(hz)) return dash;
    return std::to_string(frames) + " smp \xc2\xb7 " + valueformat::withUnit(1000.0 * frames / hz, 1, "ms");
}

std::string enginestatus::bufferTooltip(int frames, int requestedFrames, double hz)
{
    if (frames <= 0 || !validRate(hz)) return "Audio device not running";
    std::string text = "Buffer " + std::to_string(frames) + " frames = "
        + valueformat::withUnit(1000.0 * frames / hz, 2, "ms") + " at " + sampleRate(hz);
    if (requestedFrames > 0 && requestedFrames != frames)
        text += " (" + std::to_string(requestedFrames) + " requested)";
    return text;
}

std::string enginestatus::xruns(std::uint64_t count)
{
    return std::to_string(count) + (count == 1 ? " xrun" : " xruns");
}

std::string enginestatus::deviceSummary(double hz, int bufferFrames, int captureFrames, int playbackFrames,
                                        std::string_view driver)
{
    if (bufferFrames <= 0 || !validRate(hz)) return "Audio device closed";
    const auto ms = [hz](int frames) { return valueformat::fixed(1000.0 * std::max(0, frames) / hz, 1); };
    std::string text = sampleRate(hz) + " \xc2\xb7 " + std::to_string(bufferFrames) + " smp \xc2\xb7 ~"
        + ms(captureFrames) + "/" + ms(playbackFrames) + " ms";
    if (!driver.empty()) text += " \xc2\xb7 " + std::string(driver);
    return text;
}

std::string enginestatus::deviceSummaryTooltip(double hz, int bufferFrames, int captureFrames, int playbackFrames,
                                               int roundTripFrames)
{
    if (bufferFrames <= 0 || !validRate(hz)) return "Audio device closed \xc2\xb7 click for audio settings";
    const auto ms = [hz](int frames) { return valueformat::withUnit(1000.0 * std::max(0, frames) / hz, 2, "ms"); };
    std::string text = "Sample rate " + sampleRate(hz) + " \xc2\xb7 buffer " + std::to_string(bufferFrames)
        + " frames \xc2\xb7 input latency " + ms(captureFrames) + ", output latency " + ms(playbackFrames);
    if (roundTripFrames > 0) text += " \xc2\xb7 hardware round trip " + ms(roundTripFrames);
    return text + " \xc2\xb7 click for audio settings";
}

std::string enginestatus::xrunTooltip(std::uint64_t count)
{
    if (count == 0) return "Audio dropouts (xruns): none";
    return "Audio dropouts (xruns) since start or the last reset \xc2\xb7 click to reset";
}
