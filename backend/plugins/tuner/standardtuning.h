#pragma once
#include <array>
#include <cmath>
#include <limits>

namespace standardtuning {
// Indices are ordered from lowest to highest pitch; guitar string numbers are 6..1.
struct Target { const char* note; int midi; int stringNumber; };
inline constexpr std::array<Target, 6> targets{{
    {"E2", 40, 6}, {"A2", 45, 5}, {"D3", 50, 4},
    {"G3", 55, 3}, {"B3", 59, 2}, {"E4", 64, 1}
}};
struct Reading { int index = -1; double targetHz = 0; double cents = 0; };
inline Reading measure(double hz, int selection = 0) noexcept {
    if (!std::isfinite(hz) || hz <= 0) return {};
    const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
    int index = selection >= 1 && selection <= 6 ? selection - 1 : -1;
    if (index < 0) {
        double distance = std::numeric_limits<double>::max();
        for (int i = 0; i < int(targets.size()); ++i) {
            const double candidate = std::abs(midi - targets[i].midi);
            if (candidate < distance) { distance = candidate; index = i; }
        }
    }
    return {index, 440.0 * std::pow(2.0, (targets[index].midi - 69.0) / 12.0),
            (midi - targets[index].midi) * 100.0};
}
}
