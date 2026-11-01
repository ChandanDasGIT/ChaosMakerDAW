#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace tempo {
// Quarter-note BPM, with the same note ordering used by the modulation controls.
inline double noteFrequency(double bpm, int division) noexcept {
    constexpr std::array<double, 7> beats{4, 2, 1, 0.5, 0.25, 0.75, 1.0/3};
    if (!std::isfinite(bpm) || bpm <= 0) bpm = 120;
    return bpm / (60 * beats[std::clamp(division, 0, 6)]);
}
}
