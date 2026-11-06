#include "panlaw.h"

#include <algorithm>
#include <cmath>
#include <numbers>

double panlaw::clampPan(double pan) noexcept
{
    if (!std::isfinite(pan)) return centre;
    return std::clamp(pan, minPan, maxPan);
}

panlaw::Gains panlaw::balance(double pan) noexcept
{
    const double p = clampPan(pan);
    // cos(pi/2) is not exactly 0 in floating point; hard pan must be silent.
    const auto side = [](double amount) {
        return amount >= 1.0 ? 0.0f : static_cast<float>(std::cos(amount * std::numbers::pi / 2.0));
    };
    Gains gains;
    if (p > 0) gains.left = side(p);
    else if (p < 0) gains.right = side(-p);
    return gains;
}
