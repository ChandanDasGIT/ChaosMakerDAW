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
// Nonnegative elapsed time, floored to hundredths: HH:MM:SS.cc. Hours may exceed 99.
std::string position(double seconds);
// Musical position "bar.beat.hundredths", 1-based bar and beat, for a beat (in quarter notes)
// under a meter of beatsPerBar quarters with beatUnit quarters per counted beat.
// Example in 6/8 (beatsPerBar 3, beatUnit 0.5): beat 3.75 -> "2.2.50".
std::string barBeatPosition(double beat, double beatsPerBar, double beatUnit);
}
