#include "instruments/sampler/zonemap.h"

#include <algorithm>

namespace {

bool playable(const SampleZone& zone)
{
    return zone.audio && zone.audio->frames > 0 && !zone.audio->channels.empty()
        && zone.loKey >= 0 && zone.hiKey <= 127 && zone.loKey <= zone.hiKey
        && zone.loVelocity >= 0 && zone.hiVelocity <= 127 && zone.loVelocity <= zone.hiVelocity
        && zone.roundRobinLength >= 1 && zone.roundRobinPosition >= 1
        && zone.roundRobinPosition <= zone.roundRobinLength;
}

} // namespace

int ZoneMap::setZones(std::vector<SampleZone> zones)
{
    const auto before = zones.size();
    std::erase_if(zones, [](const SampleZone& zone) { return !playable(zone); });
    m_zones = std::move(zones);

    for (auto& list : m_byKey) list.clear();
    for (int i = 0; i < int(m_zones.size()); ++i)
        for (int key = m_zones[i].loKey; key <= m_zones[i].hiKey; ++key)
            m_byKey[std::size_t(key)].push_back(i);
    return int(before - m_zones.size());
}

int ZoneMap::find(int key, int velocity, Trigger trigger, std::uint32_t roundRobin,
                  std::span<int> out) const noexcept
{
    if (key < 0 || key > 127) return 0;
    const bool release = trigger == Trigger::Release;
    int count = 0;
    for (const int index : m_byKey[std::size_t(key)]) {
        if (count == int(out.size())) break;
        const auto& zone = m_zones[std::size_t(index)];
        if (zone.releaseTrigger != release) continue;
        if (velocity < zone.loVelocity || velocity > zone.hiVelocity) continue;
        if (roundRobin % std::uint32_t(zone.roundRobinLength) != std::uint32_t(zone.roundRobinPosition - 1))
            continue;
        out[std::size_t(count++)] = index;
    }
    return count;
}
