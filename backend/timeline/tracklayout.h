#pragma once

#include <vector>

// The heights a user can drag a track and its envelope lane to (restMap U9). A track starts at
// its default height and never gets smaller than that (the track header's controls need it);
// an envelope lane can shrink a little below its default.
namespace trackheight {
inline constexpr double defaultTrack = 64.0;
inline constexpr double minTrack = defaultTrack;
inline constexpr double maxTrack = 400.0;
inline constexpr double defaultEnvelope = 64.0;
inline constexpr double minEnvelope = 40.0;
inline constexpr double maxEnvelope = 400.0;
// Nonfinite values give the default.
double clampTrack(double height) noexcept;
double clampEnvelope(double height) noexcept;
}

// Vertical layout of the track rows (restMap G6). Each row is a track's own part, followed by its
// volume envelope lane when that lane is shown in its own lane (not over the clips). Pixel values
// come from the presentation layer; the rules live here so the timeline's clip drag, box
// selection and the track list agree on where every row is.
class TrackLayout {
public:
    // One row: its envelope lane flag and its own heights (restMap U9). A height <= 0 or
    // nonfinite means the layout's height for every row (trackHeight() / envelopeHeight()).
    struct Row {
        bool envelopeLane = false;
        double trackHeight = 0.0;
        double envelopeHeight = 0.0;
        bool operator==(const Row&) const = default;
    };
    // trackHeight: a track's own part; envelopeHeight: an envelope lane under it.
    explicit TrackLayout(double trackHeight = trackheight::defaultTrack, double envelopeHeight = trackheight::defaultEnvelope);
    // One flag per row, in row order: true when the row shows its envelope lane under it.
    void setRows(std::vector<bool> envelopeLanes);
    void setRowSpecs(std::vector<Row> rows);
    const std::vector<Row>& rows() const noexcept { return m_rows; }
    // Heights <= 0 or nonfinite are ignored (the old value stays).
    void setTrackHeight(double height);
    void setEnvelopeHeight(double height);

    int count() const noexcept { return static_cast<int>(m_rows.size()); }
    double trackHeight() const noexcept { return m_trackHeight; }
    double envelopeHeight() const noexcept { return m_envelopeHeight; }
    // One row's heights (its own, or the layout's); rows outside use the layout's.
    double trackHeightOf(int row) const noexcept;
    double envelopeHeightOf(int row) const noexcept;
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
    std::vector<Row> m_rows;
    std::vector<double> m_tops;   // count() + 1
    double m_trackHeight;
    double m_envelopeHeight;
    void rebuild();
};
