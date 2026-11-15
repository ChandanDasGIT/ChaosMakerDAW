#include "deviceline.h"

#include "common/core/valueformat.h"
#include "common/engine/enginestatus.h"

#include <algorithm>

std::string deviceline::summary(double hz, int bufferFrames, int captureFrames, int playbackFrames,
                               std::string_view driver)
{
    if (bufferFrames <= 0 || !enginestatus::validRate(hz)) return "Audio device closed";
    const auto ms = [hz](int frames) { return valueformat::fixed(1000.0 * std::max(0, frames) / hz, 1); };
    std::string text = enginestatus::sampleRate(hz) + " \xc2\xb7 " + std::to_string(bufferFrames) + " smp \xc2\xb7 ~"
        + ms(captureFrames) + "/" + ms(playbackFrames) + " ms";
    if (!driver.empty()) text += " \xc2\xb7 " + std::string(driver);
    return text;
}

std::string deviceline::summaryTooltip(double hz, int bufferFrames, int captureFrames, int playbackFrames,
                                      int roundTripFrames)
{
    if (bufferFrames <= 0 || !enginestatus::validRate(hz)) return "Audio device closed \xc2\xb7 click for audio settings";
    const auto ms = [hz](int frames) { return valueformat::withUnit(1000.0 * std::max(0, frames) / hz, 2, "ms"); };
    std::string text = "Sample rate " + enginestatus::sampleRate(hz) + " \xc2\xb7 buffer " + std::to_string(bufferFrames)
        + " frames \xc2\xb7 input latency " + ms(captureFrames) + ", output latency " + ms(playbackFrames);
    if (roundTripFrames > 0) text += " \xc2\xb7 hardware round trip " + ms(roundTripFrames);
    return text + " \xc2\xb7 click for audio settings";
}
