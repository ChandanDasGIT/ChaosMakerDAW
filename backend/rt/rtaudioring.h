#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <span>

// A fixed-capacity, single-writer/single-reader ring for audio samples.
// push() and pop() transfer whole spans; they never wait or allocate.
template <std::size_t Capacity>
class RtAudioRing {
    static_assert(Capacity > 0, "Capacity must be greater than zero");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    // Audio thread: writes samples, returning false if the ring has insufficient space.
    bool push(std::span<const float> samples) noexcept
    {
        const auto write = m_write.load(std::memory_order_relaxed);
        const auto read = m_read.load(std::memory_order_acquire);
        if (samples.size() > Capacity - (write - read)) return false;

        for (std::size_t i = 0; i < samples.size(); ++i)
            m_samples[(write + i) & (Capacity - 1)] = samples[i];

        m_write.store(write + samples.size(), std::memory_order_release);
        return true;
    }

    // Worker thread: reads exactly the requested number of samples, or returns false if not ready.
    bool pop(std::span<float> samples) noexcept
    {
        const auto read = m_read.load(std::memory_order_relaxed);
        const auto write = m_write.load(std::memory_order_acquire);
        if (samples.size() > write - read) return false;

        for (std::size_t i = 0; i < samples.size(); ++i)
            samples[i] = m_samples[(read + i) & (Capacity - 1)];

        m_read.store(read + samples.size(), std::memory_order_release);
        return true;
    }

    // Approximate counts are safe to query from either thread; they are intended for monitoring.
    std::size_t availableToRead() const noexcept
    {
        const auto read = m_read.load(std::memory_order_acquire);
        const auto write = m_write.load(std::memory_order_acquire);
        return write - read;
    }

    std::size_t availableToWrite() const noexcept
    {
        return Capacity - availableToRead();
    }

private:
    std::array<float, Capacity> m_samples{};
    alignas(64) std::atomic<std::size_t> m_write{0};
    alignas(64) std::atomic<std::size_t> m_read{0};
};
