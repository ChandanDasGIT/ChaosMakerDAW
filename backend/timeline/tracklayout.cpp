#include "timeline/tracklayout.h"

#include <algorithm>
#include <cmath>

double trackheight::clampTrack(double height) noexcept
{
    if (!std::isfinite(height)) return defaultTrack;
    return std::clamp(height, minTrack, maxTrack);
}

double trackheight::clampEnvelope(double height) noexcept
{
    if (!std::isfinite(height)) return defaultEnvelope;
    return std::clamp(height, minEnvelope, maxEnvelope);
}

TrackLayout::TrackLayout(double trackHeight, double envelopeHeight)
    : m_trackHeight(std::isfinite(trackHeight) && trackHeight > 0 ? trackHeight : trackheight::defaultTrack),
      m_envelopeHeight(std::isfinite(envelopeHeight) && envelopeHeight > 0 ? envelopeHeight : trackheight::defaultEnvelope)
{
    rebuild();
}

void TrackLayout::setRows(std::vector<bool> envelopeLanes)
{
    std::vector<Row> rows;
    rows.reserve(envelopeLanes.size());
    for (bool lane : envelopeLanes) rows.push_back({lane, 0.0, 0.0});
    setRowSpecs(std::move(rows));
}

void TrackLayout::setRowSpecs(std::vector<Row> rows)
{
    m_rows = std::move(rows);
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

double TrackLayout::trackHeightOf(int row) const noexcept
{
    if (row < 0 || row >= count()) return m_trackHeight;
    const double own = m_rows[static_cast<std::size_t>(row)].trackHeight;
    return std::isfinite(own) && own > 0 ? own : m_trackHeight;
}

double TrackLayout::envelopeHeightOf(int row) const noexcept
{
    if (row < 0 || row >= count()) return m_envelopeHeight;
    const double own = m_rows[static_cast<std::size_t>(row)].envelopeHeight;
    return std::isfinite(own) && own > 0 ? own : m_envelopeHeight;
}

void TrackLayout::rebuild()
{
    m_tops.assign(m_rows.size() + 1, 0.0);
    for (int i = 0; i < count(); ++i)
        m_tops[std::size_t(i) + 1] = m_tops[std::size_t(i)] + rowHeight(i);
}

bool TrackLayout::hasEnvelopeLane(int row) const noexcept
{
    return row >= 0 && row < count() && m_rows[static_cast<std::size_t>(row)].envelopeLane;
}

double TrackLayout::rowTop(int row) const noexcept
{
    if (row < 0) return row * m_trackHeight;
    if (row <= count()) return m_tops[static_cast<std::size_t>(row)];
    return m_tops.back() + (row - count()) * m_trackHeight;
}

double TrackLayout::rowHeight(int row) const noexcept
{
    return trackHeightOf(row) + (hasEnvelopeLane(row) ? envelopeHeightOf(row) : 0.0);
}

double TrackLayout::envelopeTop(int row) const noexcept
{
    return rowTop(row) + trackHeightOf(row);
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
    return row + std::min(1.0, inside / trackHeightOf(row));
}
