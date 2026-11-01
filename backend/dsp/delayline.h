#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Prepare/resize off the audio thread. The audio thread only reads/writes the
// already allocated ring buffer.
class DelayLine {
public:
    void prepare(int samples)
    {
        m_buffer.assign(static_cast<std::size_t>(std::max(samples, 2)), 0.0f);
        m_write = 0;
    }

    void reset() noexcept
    {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        m_write = 0;
    }

    float read(float delaySamples) const noexcept
    {
        const float maxDelay = static_cast<float>(m_buffer.size() - 1);
        const float delay = std::clamp(delaySamples, 1.0f, maxDelay);
        const float readPosition = static_cast<float>(m_write) - delay;
        const auto base = static_cast<int>(std::floor(readPosition));
        const float fraction = readPosition - static_cast<float>(base);
        const int size = static_cast<int>(m_buffer.size());
        const int i0 = (base % size + size) % size;
        const int i1 = (i0 + 1) % size;
        return m_buffer[static_cast<std::size_t>(i0)] * (1.0f - fraction)
             + m_buffer[static_cast<std::size_t>(i1)] * fraction;
    }

    void write(float sample) noexcept
    {
        m_buffer[static_cast<std::size_t>(m_write)] = sample;
        m_write = (m_write + 1) % static_cast<int>(m_buffer.size());
    }

private:
    std::vector<float> m_buffer;
    int m_write = 0;
};
