#pragma once
#include <algorithm>
#include <cmath>

// Prepared once; no allocation or parameter mutation in process().
class DCBlocker {
public:
    void prepare(double sampleRate, float cutoff = 20.0f) noexcept {
        coefficient = std::exp(-6.283185307179586f * cutoff / float(std::max(sampleRate, 1000.0)));
        reset();
    }
    void reset() noexcept { previousInput = previousOutput = 0; }
    float process(float input) noexcept {
        const float output = input - previousInput + coefficient * previousOutput;
        previousInput = input;
        previousOutput = output;
        return output;
    }
private:
    float coefficient = 0.999f;
    float previousInput = 0, previousOutput = 0;
};
