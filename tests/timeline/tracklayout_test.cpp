#include "testkit.h"
#include "timeline/tracklayout.h"

#include <cmath>

TEST(tracklayout, rows_without_lanes_are_fixed_steps_like_before)
{
    TrackLayout layout(106, 72);
    layout.setRows({false, false, false});
    CHECK(layout.count() == 3);
    CHECK(layout.rowTop(0) == 0 && layout.rowTop(1) == 106 && layout.rowTop(2) == 212);
    CHECK(layout.totalHeight() == 318);
    for (double y : {-50.0, 0.0, 50.0, 105.9, 106.0, 250.0, 317.0, 318.0, 500.0})
        CHECK(layout.rowAt(y) == int(std::floor(y / 106.0)));
    CHECK_NEAR(layout.rowCoordinate(159), 1.5, 1e-12);
    CHECK_NEAR(layout.rowCoordinate(424), 4.0, 1e-12);   // past the end keeps going
    CHECK(!layout.inEnvelopeLane(50));
}

TEST(tracklayout, an_envelope_lane_pushes_the_rows_below_it_down)
{
    TrackLayout layout(106, 72);
    layout.setRows({false, true, false});
    CHECK(layout.rowTop(1) == 106);
    CHECK(layout.rowHeight(1) == 178);
    CHECK(layout.envelopeTop(1) == 212);
    CHECK(layout.rowTop(2) == 284);
    CHECK(layout.totalHeight() == 390);
    const auto tops = layout.rowTops();
    CHECK(tops.size() == 4 && tops[2] == 284 && tops[3] == 390);

    CHECK(layout.rowAt(150) == 1);
    CHECK(layout.rowAt(250) == 1);   // in the lane: still track 1
    CHECK(layout.rowAt(284) == 2);
    CHECK(layout.rowAt(390) == 3);
    CHECK(layout.inEnvelopeLane(212) && layout.inEnvelopeLane(283.9));
    CHECK(!layout.inEnvelopeLane(211.9) && !layout.inEnvelopeLane(284));

    // Selection coordinates: the lane is row 1's bottom edge, then row 2 starts.
    CHECK_NEAR(layout.rowCoordinate(159), 1.5, 1e-12);
    CHECK_NEAR(layout.rowCoordinate(250), 2.0, 1e-12);
    CHECK_NEAR(layout.rowCoordinate(284 + 53), 2.5, 1e-12);
    double last = -10;
    for (double y = -20; y < 500; y += 0.5) {
        const double c = layout.rowCoordinate(y);
        CHECK(c >= last);
        last = c;
    }
}

TEST(tracklayout, heights_change_and_bad_values_are_ignored)
{
    TrackLayout layout(106, 72);
    layout.setRows({true});
    layout.setEnvelopeHeight(-1);
    layout.setTrackHeight(std::nan(""));
    CHECK(layout.totalHeight() == 178);
    layout.setEnvelopeHeight(50);
    CHECK(layout.totalHeight() == 156);
    CHECK(!layout.hasEnvelopeLane(-1) && !layout.hasEnvelopeLane(1));
}

// restMap U9: each row can have its own heights.
TEST(tracklayout, rows_have_their_own_heights)
{
    TrackLayout layout(64, 72);
    layout.setRowSpecs({{false, 100, 0}, {true, 0, 50}, {true, 80, 0}});
    CHECK(layout.trackHeightOf(0) == 100 && layout.trackHeightOf(1) == 64 && layout.trackHeightOf(2) == 80);
    CHECK(layout.envelopeHeightOf(1) == 50 && layout.envelopeHeightOf(2) == 72);
    CHECK(layout.rowTop(1) == 100);
    CHECK(layout.envelopeTop(1) == 164);
    CHECK(layout.rowTop(2) == 214);
    CHECK(layout.envelopeTop(2) == 294);
    CHECK(layout.totalHeight() == 366);
    CHECK(layout.rowAt(99) == 0 && layout.rowAt(100) == 1 && layout.rowAt(213) == 1 && layout.rowAt(214) == 2);
    CHECK(layout.inEnvelopeLane(164) && !layout.inEnvelopeLane(163.9));
    CHECK_NEAR(layout.rowCoordinate(50), 0.5, 1e-12);        // half of row 0's own 100
    CHECK_NEAR(layout.rowCoordinate(214 + 40), 2.5, 1e-12);
    CHECK(layout.rowAt(366 + 64) == 4);                     // past the end: default steps
    // The layout's heights still apply to rows without their own.
    layout.setTrackHeight(70);
    CHECK(layout.trackHeightOf(1) == 70 && layout.trackHeightOf(0) == 100);
}

TEST(tracklayout, height_limits_keep_the_default_as_the_least_track_height)
{
    CHECK(trackheight::clampTrack(10) == trackheight::defaultTrack);
    CHECK(trackheight::clampTrack(150) == 150);
    CHECK(trackheight::clampTrack(1e9) == trackheight::maxTrack);
    CHECK(trackheight::clampTrack(std::nan("")) == trackheight::defaultTrack);
    CHECK(trackheight::clampEnvelope(10) == trackheight::minEnvelope);
    CHECK(trackheight::clampEnvelope(1e9) == trackheight::maxEnvelope);
}
