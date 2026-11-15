#pragma once

#include <string>

// Audio device readouts shared by the top bar's device line and the status line.
// Pure C++; invalid values (no device yet) render as an em dash.
namespace enginestatus {
// A usable sample rate (finite, above 0).
bool validRate(double hz);
// "48 kHz", "44.1 kHz", "88.2 kHz".
std::string sampleRate(double hz);
}
