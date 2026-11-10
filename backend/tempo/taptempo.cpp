#include "tempo/taptempo.h"

#include <algorithm>
#include <cmath>

std::optional<double> TapTempo::tap(double seconds)
{
    if (!std::isfinite(seconds)) return std::nullopt;
    if (!m_taps.empty()) {
        const double gap = seconds - m_taps.back();
        if (gap <= 0.0) return std::nullopt;                      // the same moment or backwards
        if (gap > ResetAfterSeconds) m_taps.clear();               // a pause: a new count
    }
    if (m_taps.size() >= 2) {
        // A clear change of pace: start the count again from the previous tap.
        const double average = (m_taps.back() - m_taps.front()) / double(m_taps.size() - 1);
        const double gap = seconds - m_taps.back();
        if (std::abs(gap - average) > ChangeRatio * average) m_taps.erase(m_taps.begin(), m_taps.end() - 1);
    }
    m_taps.push_back(seconds);
    if (m_taps.size() > std::size_t(MaxIntervals) + 1) m_taps.erase(m_taps.begin());
    if (m_taps.size() < 2) return std::nullopt;
    const double interval = (m_taps.back() - m_taps.front()) / double(m_taps.size() - 1);
    const double bpm = std::clamp(60.0 / interval, MinBpm, MaxBpm);
    return std::round(bpm * 10.0) / 10.0;
}
