#pragma once

#include <string>
#include <vector>

// The tuner's texts: string buttons ("6 · E2" ... "1 · E4"), reference caption and readouts.
namespace tunertext {
std::vector<std::string> stringLabels();
std::string referenceText();
inline constexpr double inTuneCents = 3.0;
bool inTune(double cents) noexcept;
std::string status(bool hasPitch, bool bypassed, double cents);
std::string formatCents(double cents);                    // "+2.5 cents"
std::string formatFrequency(double hz, double targetHz);  // "110.2 Hz → 110.00 Hz"
}
