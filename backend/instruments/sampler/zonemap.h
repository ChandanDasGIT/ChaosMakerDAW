#pragma once
#include "instruments/sampler/samplezone.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Finds the zones that sound for a key press.
//
// setZones() runs off the audio thread: it drops zones that can never play and builds an
// index per key. find() then runs on the audio thread: it does not allocate and only writes
// zone indices into the caller's buffer.
class ZoneMap {
public:
    enum class Trigger { Attack, Release };            // note-on zones, or release-trigger zones

    // Keeps the playable zones in their original order and returns how many were dropped
    // (no audio unless silent, empty key or velocity range, bad round-robin numbers).
    int setZones(std::vector<SampleZone> zones);

    // Whether `zone` points into this map (a voice still playing one of its zones).
    bool holds(const SampleZone* zone) const noexcept
    {
        return zone && !m_zones.empty() && zone >= m_zones.data() && zone < m_zones.data() + m_zones.size();
    }

    // Writes the indices of the matching zones into out, in zone order, and returns how many
    // it wrote (at most out.size()). roundRobin counts earlier presses of this key: a zone
    // with length n and position p plays when roundRobin % n == p - 1.
    int find(int key, int velocity, Trigger trigger, std::uint32_t roundRobin,
             std::span<int> out) const noexcept;

    const SampleZone& zone(int index) const noexcept { return m_zones[std::size_t(index)]; }
    int size() const noexcept { return int(m_zones.size()); }

private:
    std::vector<SampleZone> m_zones;
    std::array<std::vector<int>, 128> m_byKey;         // zone indices for each key
};
