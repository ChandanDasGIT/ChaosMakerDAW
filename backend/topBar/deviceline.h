#pragma once

#include <string>
#include <string_view>

// The top bar's device line. Pure C++.
namespace deviceline {
// "48 kHz · 128 smp · ~2.7/2.7 ms · JACK" (input / output latency); "Audio device closed" when
// nothing runs. driver may be empty.
std::string summary(double hz, int bufferFrames, int captureFrames, int playbackFrames, std::string_view driver);
// Its tooltip, spelled out; adds the hardware round trip when the device reports one (> 0).
std::string summaryTooltip(double hz, int bufferFrames, int captureFrames, int playbackFrames, int roundTripFrames);
}
