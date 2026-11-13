#pragma once

#include <vector>

// Vertical layout of the track rows (restMap G6). Each row is a track's own part, followed by its
// volume envelope lane when that lane is shown in its own lane (not over the clips). Pixel values
// come from the presentation layer; the rules live here so the timeline's clip drag, box
// selection and the track list agree on where every row is.
class TrackLayout {
public:
    // trackHeight: a track's own part; envelopeHeight: an envelope lane under it.
    explicit TrackLayout(double trackHeight = 64.0, double envelopeHeight = 72.0);
    // One flag per row, in row order: true when the row shows its envelope lane under it.
    void setRows(std::vector<bool> envelopeLanes);
    // Heights <= 0 or nonfinite are ignored (the old value stays).
    void setTrackHeight(double height);
    void setEnvelopeHeight(double height);

    int count() const noexcept { return static_cast<int>(m_lanes.size()); }
    double trackHeight() const noexcept { return m_trackHeight; }
    double envelopeHeight() const noexcept { return m_envelopeHeight; }
    bool hasEnvelopeLane(int row) const noexcept;
    // Top of a row; row == count() gives the total height. Rows outside continue at trackHeight
    // steps, so a pointer past the last row still maps like before.
    double rowTop(int row) const noexcept;
    double rowHeight(int row) const noexcept;   // the track part plus its envelope lane
    double envelopeTop(int row) const noexcept;   // where the lane starts (the row's track part ends)
    double totalHeight() const noexcept { return rowTop(count()); }
    std::vector<double> rowTops() const;          // count() + 1 values; the last is the total

    // The row a y belongs to, its envelope lane included. Past the last row (or above the
    // first) it continues at trackHeight steps, like a plain floor(y / trackHeight).
    int rowAt(double y) const noexcept;
    // True when y is inside a shown envelope lane.
    bool inEnvelopeLane(double y) const noexcept;
    // y in the row units the clip selection uses: a track part covers [row, row + 1), its
    // envelope lane counts as the row's bottom edge (row + 1), so a box drawn in a lane selects
    // no clips of that row. Monotonic in y.
    double rowCoordinate(double y) const noexcept;

private:
    std::vector<bool> m_lanes;
    std::vector<double> m_tops;   // count() + 1
    double m_trackHeight;
    double m_envelopeHeight;
    void rebuild();
};
