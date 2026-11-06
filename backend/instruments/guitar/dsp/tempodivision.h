#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace tempo {
// Names of the modulation divisions, in the same order as noteFrequency's division index.
inline constexpr std::array<const char*, 7> divisionNames{
    "1/1 · Whole note", "1/2 · Half note", "1/4 · Quarter note", "1/8 · Eighth note",
    "1/16 · Sixteenth note", "1/8 · Dotted eighth", "1/8 · Eighth triplet"};
// Quarter-note BPM, with the same note ordering used by the modulation controls.
inline double noteFrequency(double bpm, int division) noexcept {
    constexpr std::array<double, 7> beats{4, 2, 1, 0.5, 0.25, 0.75, 1.0/3};
    if (!std::isfinite(bpm) || bpm <= 0) bpm = 120;
    return bpm / (60 * beats[std::clamp(division, 0, 6)]);
}
}
