#pragma once
#include "instruments/guitar/dsp/dcblocker.h"

#include <algorithm>
#include <cmath>

// One clipping stage that reacts to playing (plan 6.3). Used inside an oversampler.
//
// The curve has slope 1 at rest and saturates at 1/positiveKnee above and 1/negativeKnee
// below; unequal knees give even harmonics. Swings above `conduction` charge a bias that
// pushes the curve off centre (like a tube's grid or a starved transistor): hard picking
// changes the clipping, then it recovers. A DC blocker removes the offset this creates.
struct DriveStage {
    float positiveKnee = 1.0f, negativeKnee = 1.0f;
    float conduction = 0.6f, biasDepth = 0.5f;
    float attack = 0.01f, release = 0.001f;
    float bias = 0.0f;
    DCBlocker dc;

    // rate: the (oversampled) rate the stage runs at.
    void prepare(double rate, float attackMs, float releaseMs) noexcept
    {
        const auto coefficient = [rate](float ms) {
            return float(1.0 - std::exp(-1.0 / (std::max(rate, 1000.0) * double(ms) * 0.001)));
        };
        attack = coefficient(attackMs);
        release = coefficient(releaseMs);
        dc.prepare(rate);
        reset();
    }

    void reset() noexcept
    {
        bias = 0.0f;
        dc.reset();
    }

    static float curve(float x, float positiveKnee, float negativeKnee) noexcept
    {
        return x >= 0.0f ? std::tanh(positiveKnee * x) / positiveKnee : std::tanh(negativeKnee * x) / negativeKnee;
    }

    float process(float x) noexcept
    {
        const float y = curve(x - biasDepth * bias, positiveKnee, negativeKnee);
        const float charge = std::max(0.0f, x - conduction);
        bias += (charge > bias ? attack : release) * (charge - bias);
        return dc.process(y);
    }
};
