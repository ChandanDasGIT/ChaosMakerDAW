#include "timeline/tracklayout.h"

#include <algorithm>
#include <cmath>

TrackLayout::TrackLayout(double trackHeight, double envelopeHeight)
    : m_trackHeight(std::isfinite(trackHeight) && trackHeight > 0 ? trackHeight : 64.0),
      m_envelopeHeight(std::isfinite(envelopeHeight) && envelopeHeight > 0 ? envelopeHeight : 72.0)
{
    rebuild();
}

void TrackLayout::setRows(std::vector<bool> envelopeLanes)
{
    m_lanes = std::move(envelopeLanes);
    rebuild();
}

void TrackLayout::setTrackHeight(double height)
{
    if (!std::isfinite(height) || height <= 0) return;
    m_trackHeight = height;
    rebuild();
}

void TrackLayout::setEnvelopeHeight(double height)
{
    if (!std::isfinite(height) || height <= 0) return;
    m_envelopeHeight = height;
    rebuild();
}

void TrackLayout::rebuild()
{
    m_tops.assign(m_lanes.size() + 1, 0.0);
    for (std::size_t i = 0; i < m_lanes.size(); ++i)
        m_tops[i + 1] = m_tops[i] + m_trackHeight + (m_lanes[i] ? m_envelopeHeight : 0.0);
}

bool TrackLayout::hasEnvelopeLane(int row) const noexcept
{
    return row >= 0 && row < count() && m_lanes[static_cast<std::size_t>(row)];
}

double TrackLayout::rowTop(int row) const noexcept
{
    if (row < 0) return row * m_trackHeight;
    if (row <= count()) return m_tops[static_cast<std::size_t>(row)];
    return m_tops.back() + (row - count()) * m_trackHeight;
}

double TrackLayout::rowHeight(int row) const noexcept
{
    return m_trackHeight + (hasEnvelopeLane(row) ? m_envelopeHeight : 0.0);
}

double TrackLayout::envelopeTop(int row) const noexcept
{
    return rowTop(row) + m_trackHeight;
}

std::vector<double> TrackLayout::rowTops() const
{
    return m_tops;
}

int TrackLayout::rowAt(double y) const noexcept
{
    if (!std::isfinite(y)) return -1;
    const double total = m_tops.back();
    if (y < 0) return static_cast<int>(std::floor(y / m_trackHeight));
    if (y >= total) return count() + static_cast<int>(std::floor((y - total) / m_trackHeight));
    const auto next = std::upper_bound(m_tops.begin(), m_tops.end(), y);
    return static_cast<int>(next - m_tops.begin()) - 1;
}

bool TrackLayout::inEnvelopeLane(double y) const noexcept
{
    const int row = rowAt(y);
    return hasEnvelopeLane(row) && y >= envelopeTop(row);
}

double TrackLayout::rowCoordinate(double y) const noexcept
{
    if (!std::isfinite(y)) return 0.0;
    const double total = m_tops.back();
    if (y < 0) return y / m_trackHeight;
    if (y >= total) return count() + (y - total) / m_trackHeight;
    const int row = rowAt(y);
    const double inside = y - rowTop(row);
    return row + std::min(1.0, inside / m_trackHeight);
}
