#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Status strip readouts for the audio device: sample rate, buffer size / latency, xruns.
// Pure C++; invalid values (no device yet) render as an em dash.
namespace enginestatus {
// "48 kHz", "44.1 kHz", "88.2 kHz".
std::string sampleRate(double hz);
// "128 smp · 2.7 ms" (one buffer of audio).
std::string buffer(int frames, double hz);
// "Buffer 128 frames = 2.67 ms at 48 kHz", plus " (64 requested)" when JACK runs another size.
std::string bufferTooltip(int frames, int requestedFrames, double hz);
// "0 xruns", "1 xrun", "12 xruns".
std::string xruns(std::uint64_t count);
std::string xrunTooltip(std::uint64_t count);
// The top bar's device line, like Reaper's: "48 kHz · 128 smp · ~2.7/2.7 ms · JACK" (input /
// output latency); "Audio device closed" when nothing runs. driver may be empty.
std::string deviceSummary(double hz, int bufferFrames, int captureFrames, int playbackFrames,
                          std::string_view driver);
// Its tooltip, spelled out; adds the hardware round trip when the device reports one (> 0).
std::string deviceSummaryTooltip(double hz, int bufferFrames, int captureFrames, int playbackFrames,
                                 int roundTripFrames);
}
