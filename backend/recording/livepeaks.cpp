#include "recording/livepeaks.h"

#include <algorithm>
#include <cmath>

void LivePeaks::append(const float* interleaved, std::size_t frames, unsigned channels)
{
    if (!interleaved || channels == 0) return;
    std::lock_guard lock(m_mutex);
    for (std::size_t f = 0; f < frames; ++f) {
        for (unsigned c = 0; c < channels; ++c) {
            float v = interleaved[f * channels + c];
            if (!std::isfinite(v)) v = 0.0f;
            if (m_partialFrames == 0 && c == 0) m_partial = {v, v};
            m_partial.first = std::min(m_partial.first, v);
            m_partial.second = std::max(m_partial.second, v);
        }
        if (++m_partialFrames == FramesPerPeak) {
            m_peaks.push_back(m_partial);
            m_partialFrames = 0;
        }
        ++m_frames;
    }
}

std::int64_t LivePeaks::frames() const
{
    std::lock_guard lock(m_mutex);
    return m_frames;
}

std::vector<std::pair<float, float>> LivePeaks::summary(int bins, double from, double to) const
{
    std::lock_guard lock(m_mutex);
    std::vector<std::pair<float, float>> result;
    const std::size_t groups = m_peaks.size() + (m_partialFrames > 0 ? 1 : 0);
    if (!std::isfinite(from) || !std::isfinite(to)) return result;
    from = std::clamp(from, 0.0, 1.0);
    to = std::clamp(to, 0.0, 1.0);
    if (to <= from) return result;
    const std::size_t first = std::min(groups, std::size_t(std::floor(from * double(groups))));
    const std::size_t last = std::min(groups, std::size_t(std::ceil(to * double(groups))));
    if (bins < 1 || last <= first) return result;
    const std::size_t span = last - first;
    const auto group = [&](std::size_t i) { return i < m_peaks.size() ? m_peaks[i] : m_partial; };
    result.reserve(std::size_t(bins));
    for (int b = 0; b < bins; ++b) {
        // Each bin covers its share of the groups; with more bins than groups a group repeats.
        const std::size_t begin = first + std::size_t(b) * span / std::size_t(bins);
        const std::size_t end = std::max(begin + 1, first + std::size_t(b + 1) * span / std::size_t(bins));
        auto peak = group(begin);
        for (std::size_t i = begin + 1; i < end && i < last; ++i) {
            peak.first = std::min(peak.first, group(i).first);
            peak.second = std::max(peak.second, group(i).second);
        }
        result.push_back(peak);
    }
    return result;
}
