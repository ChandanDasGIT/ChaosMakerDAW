#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

// One writing thread, one reading thread, no locks. T must be plain data, so copying can never allocate.
template <typename T, std::size_t Capacity>
class RtQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "copying T must never allocate");

public:
    bool push(const T& item) noexcept {                   // false when full
        const auto write = m_write.load(std::memory_order_relaxed);
        if (write - m_read.load(std::memory_order_acquire) == Capacity) return false;
        m_items[write & (Capacity - 1)] = item;
        m_write.store(write + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept {                          // false when empty
        const auto read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire)) return false;
        item = m_items[read & (Capacity - 1)];
        m_read.store(read + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> m_items{};
    alignas(64) std::atomic<std::size_t> m_write{0};
    alignas(64) std::atomic<std::size_t> m_read{0};
};
