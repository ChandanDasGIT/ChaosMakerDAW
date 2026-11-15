#pragma once

// Track pan: -1 = hard left, 0 = centre, +1 = hard right.
namespace panlaw {
inline constexpr double minPan = -1.0;
inline constexpr double maxPan = 1.0;
inline constexpr double centre = 0.0;

struct Gains {
    float left = 1.0f;
    float right = 1.0f;
};

// Into [-1, 1]; non-finite values are centre.
double clampPan(double pan) noexcept;
// Stereo balance with a sine curve: centre leaves both channels at unity (no level change),
// panning turns only the opposite channel down (cos), so a hard-panned stereo track is never
// boosted. Hard left/right silences the other channel.
Gains balance(double pan) noexcept;
}
