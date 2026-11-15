#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// The status line's readouts: buffer size / latency, xruns, and its one-line message.
// Pure C++; invalid values (no device yet) render as an em dash.
namespace statusstrip {
// "128 smp · 2.7 ms" (one buffer of audio).
std::string buffer(int frames, double hz);
// "Buffer 128 frames = 2.67 ms at 48 kHz", plus " (64 requested)" when JACK runs another size.
std::string bufferTooltip(int frames, int requestedFrames, double hz);
// "0 xruns", "1 xrun", "12 xruns".
std::string xruns(std::uint64_t count);
std::string xrunTooltip(std::uint64_t count);
// The message on one line: the transport message when there is one, else "Engine <version>  ·
// <status>"; line breaks (with the space around them) become " · ".
std::string message(std::string_view transportMessage, std::string_view version, std::string_view status);
}
