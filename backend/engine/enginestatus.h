#pragma once

#include <cstdint>
#include <string>

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
}
