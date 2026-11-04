#pragma once

#include "midi/midievent.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>

class MidiTakeRecorder {
public:
    explicit MidiTakeRecorder(int trackId, std::int64_t startSample = 0) noexcept
        : m_trackId(trackId), m_startSample(startSample) {}

    void begin(std::int64_t position) noexcept {
        m_startSample = position;
        m_activeNotes.fill(ActiveNote{});
        m_capturedCount = 0;
        m_finished.store(false, std::memory_order_relaxed);
    }

    void capture(const MidiEvent& ev, std::int64_t blockPosition) noexcept {
        const std::int64_t sampleTime = blockPosition + ev.offset;
        const uint8_t status = ev.status & 0xF0;
        const uint8_t key = ev.data1 & 0x7F;
        const uint8_t vel = ev.data2 & 0x7F;

        if (status == 0x90 && vel > 0) {
            if (m_activeNotes[key].active) {
                closeNote(key, sampleTime);
            }
            m_activeNotes[key] = ActiveNote{true, sampleTime, vel};
        } else if (status == 0x80 || (status == 0x90 && vel == 0)) {
            if (m_activeNotes[key].active) {
                closeNote(key, sampleTime);
            }
        }
    }

    void finish(std::int64_t endPosition) noexcept {
        for (int k = 0; k < 128; ++k) {
            if (m_activeNotes[k].active) {
                closeNote(k, endPosition);
            }
        }
        m_endSample = std::max(m_startSample, endPosition);
        m_finished.store(true, std::memory_order_release);
    }

    bool isFinished() const noexcept {
        return m_finished.load(std::memory_order_acquire);
    }

    struct CapturedNote {
        std::int64_t on = 0;
        std::int64_t off = 0;
        uint8_t key = 60;
        uint8_t velocity = 100;
    };

    std::span<const CapturedNote> notes() const noexcept {
        return {m_capturedNotes.data(), m_capturedCount};
    }

    int trackId() const noexcept { return m_trackId; }
    std::int64_t startSample() const noexcept { return m_startSample; }
    std::int64_t endSample() const noexcept { return m_endSample; }

private:
    struct ActiveNote {
        bool active = false;
        std::int64_t on = 0;
        uint8_t velocity = 0;
    };

    void closeNote(int key, std::int64_t offSample) noexcept {
        if (m_capturedCount < m_capturedNotes.size()) {
            const auto& act = m_activeNotes[key];
            const std::int64_t finalOff = std::max(offSample, act.on + 1);
            m_capturedNotes[m_capturedCount++] = CapturedNote{
                act.on,
                finalOff,
                static_cast<uint8_t>(key),
                act.velocity
            };
        }
        m_activeNotes[key] = ActiveNote{};
    }

    int m_trackId = -1;
    std::int64_t m_startSample = 0;
    std::int64_t m_endSample = 0;
    std::array<ActiveNote, 128> m_activeNotes{};
    std::array<CapturedNote, 4096> m_capturedNotes{};
    std::size_t m_capturedCount = 0;
    std::atomic<bool> m_finished{false};
};
