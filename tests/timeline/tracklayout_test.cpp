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
