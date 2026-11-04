#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

struct MidiEvent {
    int offset = 0;                                       // sample inside this block
    std::uint8_t status = 0, data1 = 0, data2 = 0;
};

// Fixed size, so it never allocates. Processors may read and rewrite it: ChordPlayer does.
class MidiBuffer {
public:
    void clear() noexcept { m_count = 0; }
    bool add(const MidiEvent& event) noexcept {
        if (m_count == m_events.size()) return false;
        m_events[m_count++] = event;
        return true;
    }
    std::span<const MidiEvent> events() const noexcept { return {m_events.data(), m_count}; }
    std::span<MidiEvent> events() noexcept { return {m_events.data(), m_count}; }

    void sort() noexcept {
        std::stable_sort(m_events.begin(), m_events.begin() + m_count, [](const MidiEvent& a, const MidiEvent& b) {
            if (a.offset != b.offset) return a.offset < b.offset;
            auto priority = [](const MidiEvent& e) {
                if ((e.status & 0xF0) == 0xB0 && e.data1 == 123) return 3; // all notes off first
                if ((e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0)) return 2; // note-off
                return 1; // note-on and others
            };
            return priority(a) > priority(b);
        });
    }

private:
    std::array<MidiEvent, 1024> m_events{};
    std::size_t m_count = 0;
};
