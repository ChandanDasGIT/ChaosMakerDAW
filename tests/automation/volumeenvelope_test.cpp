#include "testkit.h"
#include "automation/volumeenvelope.h"

#include <cmath>
#include <limits>

using automation::EnvelopePoint;
using automation::VolumeEnvelope;
namespace scale = automation::envelopescale;

TEST(volumeenvelope, the_fader_shaped_scale_runs_from_silence_to_plus_six_db)
{
    CHECK(scale::gainForLevel(0.0) == 0.0);
    CHECK(std::isinf(scale::dbForLevel(0.0)) && scale::dbForLevel(0.0) < 0);
    CHECK_NEAR(scale::dbForLevel(1.0), 6.0, 1e-9);
    CHECK_NEAR(scale::gainForLevel(scale::unityLevel()), 1.0, 1e-12);
    CHECK(scale::unityLevel() > 0.75 && scale::unityLevel() < 0.85);   // 0 dB about 4/5 up
    for (double db : {-60.0, -20.0, -6.0, 0.0, 3.0, 6.0})
        CHECK_NEAR(scale::dbForLevel(scale::levelForDb(db)), db, 1e-9);
    CHECK(scale::levelForDb(-std::numeric_limits<double>::infinity()) == 0.0);
    CHECK(scale::levelForDb(12.0) == 1.0);
    CHECK(scale::levelForDb(std::nan("")) == 0.0);
    CHECK(scale::gainForLevel(2.0) == scale::gainForLevel(1.0));
    CHECK(scale::gainForLevel(std::nan("")) == 0.0);
}

TEST(volumeenvelope, no_points_or_bypass_leaves_the_sound_unchanged)
{
    VolumeEnvelope envelope;
    CHECK(!envelope.active());
    CHECK(envelope.gainAt(0) == 1.0 && envelope.gainAt(1'000'000) == 1.0);
    CHECK_NEAR(envelope.levelAt(500), scale::unityLevel(), 1e-12);
    CHECK(envelope.addPoint(100, 0.0) == 0);
    CHECK(envelope.active() && envelope.gainAt(100) == 0.0);
    envelope.setBypassed(true);
    CHECK(!envelope.active() && envelope.gainAt(100) == 1.0);
    CHECK(envelope.levelAt(100) == 0.0);   // the drawn line stays put while bypassed
}

TEST(volumeenvelope, levels_hold_outside_the_points_and_follow_straight_lines_between)
{
    VolumeEnvelope envelope;
    CHECK(envelope.addPoint(1000, 1.0) == 0);
    CHECK(envelope.addPoint(3000, 0.0) == 1);
    CHECK(envelope.addPoint(2000, 0.5) == 1);   // inserted in order
    CHECK(envelope.levelAt(0) == 1.0 && envelope.levelAt(1000) == 1.0);
    CHECK_NEAR(envelope.levelAt(1500), 0.75, 1e-12);
    CHECK_NEAR(envelope.levelAt(2500), 0.25, 1e-12);
    CHECK(envelope.levelAt(3000) == 0.0 && envelope.levelAt(99999) == 0.0);
    // The engine follows the drawn line: gain is the scale of the interpolated level.
    CHECK_NEAR(envelope.gainAt(1500), scale::gainForLevel(0.75), 1e-12);
}

TEST(volumeenvelope, editing_rules_keep_points_ordered_and_in_range)
{
    VolumeEnvelope envelope;
    CHECK(envelope.addPoint(-1, 0.5) == -1);
    CHECK(envelope.addPoint(10, std::nan("")) == -1);
    CHECK(envelope.addPoint(10, 3.0) == 0);
    CHECK(envelope.points()[0].level == 1.0);                     // clamped
    CHECK(envelope.addPoint(10, 0.2) == -1);                      // that sample already has one
    CHECK(envelope.addPoint(100, 0.2) == 1);
    CHECK(envelope.addPoint(200, 0.2) == 2);

    CHECK(envelope.movePoint(1, 500, -1.0));                      // stops before its right neighbour
    CHECK(envelope.points()[1].position == 199 && envelope.points()[1].level == 0.0);
    CHECK(envelope.movePoint(1, 0, 0.5));                         // and after its left one
    CHECK(envelope.points()[1].position == 11);
    CHECK(envelope.movePoint(0, -50, 0.5));
    CHECK(envelope.points()[0].position == 0);
    CHECK(!envelope.movePoint(3, 0, 0.5) && !envelope.movePoint(-1, 0, 0.5));
    CHECK(!envelope.movePoint(0, 0, std::nan("")));

    CHECK(envelope.removePoint(1));
    CHECK(envelope.points().size() == 2 && envelope.points()[1].position == 200);
    CHECK(!envelope.removePoint(2) && !envelope.removePoint(-1));
}

TEST(volumeenvelope, loaded_points_are_validated_and_rescaled_for_another_rate)
{
    VolumeEnvelope envelope;
    CHECK(envelope.setPoints({{0, 0.5}, {44100, 1.0}, {88200, 0.0}}));
    CHECK(!envelope.setPoints({{10, 0.5}, {10, 0.6}}));           // not strictly increasing
    CHECK(!envelope.setPoints({{-1, 0.5}}));
    CHECK(!envelope.setPoints({{0, 1.5}}));
    CHECK(envelope.points().size() == 3);                         // a refused load changes nothing

    envelope.rescale(48000.0 / 44100.0);
    CHECK(envelope.points()[1].position == 48000 && envelope.points()[2].position == 96000);

    CHECK(envelope.setPoints({{0, 0.1}, {1, 0.2}, {2, 0.3}}));
    envelope.rescale(0.25);                                       // all land on sample 0 or 1
    CHECK(envelope.points().size() == 2);
    CHECK(envelope.points()[0] == (EnvelopePoint{0, 0.2}) && envelope.points()[1] == (EnvelopePoint{1, 0.3}));
}

TEST(volumeenvelope, restoring_content_keeps_the_view_state)
{
    VolumeEnvelope a, b;
    a.addPoint(5, 0.5);
    a.setBypassed(true);
    b.setShown(true);
    b.setOverClips(true);
    CHECK(!b.sameContent(a));
    b.restoreContent(a);
    CHECK(b.sameContent(a) && b.shown() && b.overClips());
}

TEST(volumeenvelope, curve_shapes_run_from_the_point_to_the_next)
{
    using automation::CurveShape;
    using automation::curveProgress;
    for (int i = 0; i < automation::curveShapeCount; ++i) {
        const auto shape = static_cast<CurveShape>(i);
        CHECK(curveProgress(shape, 0.0) == 0.0);
        CHECK(curveProgress(shape, 1.0) == 1.0);
        double last = 0.0;
        for (double t = 0.0; t <= 1.0; t += 0.01) {   // never turns back
            const double p = curveProgress(shape, t);
            CHECK(p >= last - 1e-12 && p >= 0.0 && p <= 1.0);
            last = p;
        }
        CHECK(automation::curveShapeFromId(automation::curveShapeId(shape)) == shape);
    }
    CHECK_NEAR(curveProgress(CurveShape::Linear, 0.25), 0.25, 1e-12);
    CHECK_NEAR(curveProgress(CurveShape::Smooth, 0.5), 0.5, 1e-12);
    CHECK(curveProgress(CurveShape::Smooth, 0.1) < 0.1 && curveProgress(CurveShape::Smooth, 0.9) > 0.9);
    CHECK(curveProgress(CurveShape::FastStart, 0.25) > 0.5);
    CHECK(curveProgress(CurveShape::FastEnd, 0.75) < 0.5);
    CHECK(curveProgress(CurveShape::Hold, 0.999) == 0.0);
    CHECK(!automation::curveShapeFromId("bezier"));
    CHECK(!automation::curveShapeFromId(""));
}

TEST(volumeenvelope, a_points_shape_bends_its_segment_and_survives_edits)
{
    using automation::CurveShape;
    VolumeEnvelope envelope;
    CHECK(envelope.addPoint(0, 0.0) == 0);
    CHECK(envelope.addPoint(1000, 1.0) == 1);
    CHECK(envelope.points()[0].shape == CurveShape::Linear);   // new points are straight
    CHECK_NEAR(envelope.levelAt(250), 0.25, 1e-12);

    CHECK(envelope.setPointShape(0, CurveShape::Hold));
    CHECK(envelope.levelAt(999) == 0.0);
    CHECK(envelope.levelAt(1000) == 1.0);
    CHECK(envelope.setPointShape(0, CurveShape::FastStart));
    CHECK(envelope.levelAt(250) > 0.5);
    CHECK(envelope.setPointShape(1, CurveShape::Smooth));       // the last point: nothing to bend
    CHECK(envelope.levelAt(5000) == 1.0);
    CHECK(!envelope.setPointShape(2, CurveShape::Linear));
    CHECK(!envelope.setPointShape(-1, CurveShape::Linear));
    CHECK(!envelope.setPointShape(0, static_cast<CurveShape>(99)));

    // Moving and rescaling keep the shape; a point added inside the segment is straight and the
    // earlier point keeps its shape up to it.
    CHECK(envelope.movePoint(0, 10, 0.1));
    CHECK(envelope.points()[0].shape == CurveShape::FastStart);
    envelope.rescale(2.0);
    CHECK(envelope.points()[0].shape == CurveShape::FastStart && envelope.points()[1].shape == CurveShape::Smooth);
    CHECK(envelope.addPoint(1000, 0.5) == 1);
    CHECK(envelope.points()[0].shape == CurveShape::FastStart && envelope.points()[1].shape == CurveShape::Linear);

    // The shape is part of the undoable content; an unknown shape does not load.
    VolumeEnvelope copy;
    copy.restoreContent(envelope);
    CHECK(copy.sameContent(envelope));
    CHECK(copy.setPointShape(0, CurveShape::Linear));
    CHECK(!copy.sameContent(envelope));
    CHECK(!copy.setPoints({{0, 0.5, static_cast<CurveShape>(7)}}));
    CHECK(copy.points().size() == 3);
}
