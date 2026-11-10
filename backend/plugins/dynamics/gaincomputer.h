#pragma once

#include <algorithm>
#include <cmath>

// Shared by the compressor and the de-esser (restMap G7).
namespace dynamics {

// The slope that gives a ratio when the detector hears the input (feed-forward)...
inline float feedForwardSlope(float ratio) noexcept { return 1.0f - 1.0f / std::max(ratio, 1.0f); }
// ...and when it hears the already reduced output (feedback): with the reduction R = (out - T) *
// slope and out = in - R, a slope of ratio - 1 gives R = (in - T) * (1 - 1 / ratio), so the
// ratio means the same in both.
inline float feedbackSlope(float ratio) noexcept { return std::max(ratio, 1.0f) - 1.0f; }

// How far a signal at inputDb is turned down (dB, >= 0): nothing below the knee, (input -
// threshold) * slope above it, and a smooth quadratic bend across a knee of kneeDb centred on
// the threshold (Giannoulis, Massberg and Reiss, "Digital Dynamic Range Compressor Design",
// JAES 2012).
inline float reductionDb(float inputDb, float thresholdDb, float slope, float kneeDb) noexcept
{
    const float over = inputDb - thresholdDb;
    if (kneeDb > 0.0f && std::abs(over) <= kneeDb * 0.5f) {
        const float x = over + kneeDb * 0.5f;
        return slope * x * x / (2.0f * kneeDb);
    }
    return over > 0.0f ? slope * over : 0.0f;
}

// A one-pole step toward a target, reaching about 63 % in `ms`.
inline float coefficient(double sampleRate, float ms) noexcept
{
    return 1.0f - std::exp(-1.0f / float(sampleRate * std::max(ms, 0.01f) * 0.001));
}

inline float toDb(float amplitude) noexcept { return 20.0f * std::log10(std::max(amplitude, 1e-9f)); }
inline float fromDb(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

} // namespace dynamics
