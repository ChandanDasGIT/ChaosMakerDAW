#pragma once

#include <string>
#include <string_view>

// Ready-to-display strings, prepared outside the audio callback. Locale-independent.
namespace valueformat {
// Nonfinite values, unsupported precision and out-of-range positions render as an em dash.
std::string number(double value);
std::string fixed(double value, int decimals);
std::string withUnit(double value, int decimals, std::string_view unit);
// Matches the existing meter display: readings at/below -90 dBFS render as negative infinity.
std::string meterLevel(double db);
// Nonnegative elapsed time, floored to milliseconds: HH:MM:SS.mmm. Hours may exceed 99.
std::string position(double seconds);
}
