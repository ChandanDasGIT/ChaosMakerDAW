#pragma once
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

private:
    std::array<MidiEvent, 1024> m_events{};
    std::size_t m_count = 0;
};
