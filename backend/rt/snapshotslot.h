#pragma once
#include "rt/rtqueue.h"

#include <atomic>

// Hands a new T from the UI thread to the audio thread, and hands the old one back so the UI thread can free it.
template <typename T>
class SnapshotSlot {
public:
    // UI thread. Only when pendingEmpty() is true.
    void publish(T* next) noexcept { m_pending.store(next, std::memory_order_release); }
    // UI thread: replace an unconsumed publication. The returned pointer was never
    // acquired by audio and can be destroyed here. Atomic exchange resolves the race.
    T* replacePending(T* next) noexcept { return m_pending.exchange(next, std::memory_order_acq_rel); }
    bool pendingEmpty() const noexcept { return m_pending.load(std::memory_order_acquire) == nullptr; }

    // Audio thread, top of every block.
    T* acquire() noexcept {
        if (T* next = m_pending.exchange(nullptr, std::memory_order_acq_rel)) {
            T* old = m_current;
            m_current = next;
            if (old) m_retired.push(old);
        }
        return m_current;
    }

    // UI thread: keep calling until nullptr, and delete each one. Call it often, m_retired is finite.
    T* collect() noexcept {
        T* item = nullptr;
        return m_retired.pop(item) ? item : nullptr;
    }

    // UI thread only, after the audio device has stopped.
    T* releaseCurrentAfterStop() noexcept
    {
        T* current = m_current;
        m_current = nullptr;
        return current;
    }

    T* cancelPendingAfterStop() noexcept
    {
        return m_pending.exchange(nullptr, std::memory_order_acq_rel);
    }

private:
    std::atomic<T*> m_pending{nullptr};
    T* m_current = nullptr;                               // audio thread only
    RtQueue<T*, 64> m_retired;
};
