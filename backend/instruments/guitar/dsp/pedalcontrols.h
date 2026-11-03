#pragma once
#include "processor/processor.h"
#include "processor/smoother.h"
#include <algorithm>
#include <array>
#include <cmath>

// Parameter smoothing and stereo peak detection shared by the Phase 2 pedals.
namespace pedal {
constexpr float pi = 3.14159265358979323846f;
inline float gain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
inline float coefficient(double rate, float ms) noexcept {
    return 1.0f - std::exp(-1.0f / float(rate * ms * 0.001));
}
inline float peak(const ProcessContext& ctx, int frame) noexcept {
    float value = 0;
    for (int c = 0; c < std::min(ctx.audio.channelCount, 2); ++c)
        value = std::max(value, std::abs(ctx.audio.channel(c)[frame]));
    return value;
}
}
