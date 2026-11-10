#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Building blocks of the mixing reverbs (restMap G7: Reverb and Chaos Verb). Real-time safe:
// memory is taken in prepare(), never while processing.
namespace reverb {

// A circular delay line read at a whole or fractional delay (linear interpolation).
class Delay {
public:
    void prepare(int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 4) size <<= 1;
        m_buffer.assign(std::size_t(size), 0.0f);
        m_mask = size - 1;
        m_write = 0;
    }
    void clear() noexcept { std::fill(m_buffer.begin(), m_buffer.end(), 0.0f); m_write = 0; }
    int capacity() const noexcept { return m_mask - 2; }
    void push(float x) noexcept
    {
        m_buffer[std::size_t(m_write)] = x;
        m_write = (m_write + 1) & m_mask;
    }
    // The sample pushed `delay` pushes ago (1 = the last one).
    float read(int delay) const noexcept
    {
        delay = std::clamp(delay, 1, capacity());
        return m_buffer[std::size_t((m_write - delay) & m_mask)];
    }
    float readFractional(float delay) const noexcept
    {
        delay = std::clamp(delay, 1.0f, float(capacity() - 1));
        const int whole = int(delay);
        const float frac = delay - float(whole);
        const float a = read(whole), b = read(whole + 1);
        return a + frac * (b - a);
    }

private:
    std::vector<float> m_buffer;
    int m_mask = 0;
    int m_write = 0;
};

// A Schroeder allpass: smears a sound in time without colouring its spectrum.
class Allpass {
public:
    void prepare(int maxDelaySamples) { m_line.prepare(maxDelaySamples); }
    void clear() noexcept { m_line.clear(); }
    float process(float x, float delay, float gain) noexcept
    {
        const float delayed = m_line.readFractional(delay);
        const float v = x + gain * delayed;
        m_line.push(v);
        return delayed - gain * v;
    }

private:
    Delay m_line;
};

// A one-pole low-pass (6 dB/oct); high() is its complement.
struct OnePole {
    float a = 1.0f;
    float z = 0.0f;
    void set(float hz, double sampleRate) noexcept
    {
        const float x = std::exp(-2.0f * 3.14159265f * std::clamp(hz, 10.0f, float(sampleRate * 0.49)) / float(sampleRate));
        a = 1.0f - x;
    }
    float low(float x) noexcept { z += a * (x - z); return z; }
    float high(float x) noexcept { return x - low(x); }
};

// An 8 x 8 Hadamard matrix times v, scaled to keep energy (orthogonal): the feedback mix of an
// 8-line delay network, so no line's energy is lost or gained by the mixing.
inline void hadamard8(std::array<float, 8>& v) noexcept
{
    for (int span = 1; span < 8; span <<= 1)
        for (int i = 0; i < 8; i += span << 1)
            for (int j = i; j < i + span; ++j) {
                const float a = v[std::size_t(j)], b = v[std::size_t(j + span)];
                v[std::size_t(j)] = a + b;
                v[std::size_t(j + span)] = a - b;
            }
    constexpr float scale = 0.35355339059327373f;   // 1 / sqrt(8)
    for (auto& x : v) x *= scale;
}

// The loop gain that makes a delay of `samples` fall 60 dB in `seconds`.
inline float decayGain(float samples, float seconds, double sampleRate) noexcept
{
    return std::pow(10.0f, -3.0f * samples / (std::max(seconds, 0.05f) * float(sampleRate)));
}

// The signs with which each of the 8 lines reaches the left and the right output: different
// patterns, so the two sides are decorrelated (a wide stereo tail from a mono input).
inline constexpr std::array<float, 8> leftTaps{1, -1, 1, 1, -1, 1, -1, -1};
inline constexpr std::array<float, 8> rightTaps{1, 1, -1, 1, 1, -1, -1, 1};

} // namespace reverb
