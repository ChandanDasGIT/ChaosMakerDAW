#include "testkit.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QPointF>
#include <QVariantMap>

#include <cmath>

namespace {
QVariant role(TrackListWrapper& tracks, int row, int role) { return tracks.data(tracks.index(row), role); }
}

TEST(qtvolumeenvelope, points_are_edited_in_beats_and_undo_as_one_step_per_drag)
{
    TrackListWrapper tracks;
    const int track = tracks.addAudioTrack();
    const double unity = tracks.envelopeUnityLevel();
    CHECK(role(tracks, 0, TrackListWrapper::EnvelopePointsRole).toList().isEmpty());

    int changes = 0;
    QObject::connect(&tracks, &QAbstractItemModel::dataChanged, [&] { ++changes; });
    CHECK(tracks.addEnvelopePoint(track, 2.0, unity) == 0);
    CHECK(tracks.addEnvelopePoint(track, 4.0, 0.5) == 1);
    CHECK(tracks.addEnvelopePoint(track, -1.0, 0.5) == -1);
    CHECK(changes == 2);
    const auto& points = tracks.project().findTrack(track)->volumeEnvelope.points();
    CHECK(points.size() == 2 && points[0].position == 48000 && points[1].position == 96000);   // 120 BPM, 48 kHz

    auto list = role(tracks, 0, TrackListWrapper::EnvelopePointsRole).toList();
    CHECK(list.size() == 2);
    CHECK_NEAR(list[1].toMap()["beat"].toDouble(), 4.0, 1e-9);
    CHECK_NEAR(list[1].toMap()["level"].toDouble(), 0.5, 1e-9);

    // One drag (first step plus continued steps) is one undo step; it cannot pass a neighbour.
    CHECK(tracks.moveEnvelopePoint(track, 1, 3.0, 0.4));
    CHECK(tracks.moveEnvelopePoint(track, 1, 1.0, 0.3, true));
    CHECK(points[1].position == 48001);
    CHECK(tracks.undo());
    CHECK(points[1].position == 96000 && points[1].level == 0.5);
    CHECK(tracks.redo());
    CHECK(tracks.undo());

    // A drag past the start stops at 0.
    CHECK(tracks.moveEnvelopePoint(track, 0, -3.0, unity));
    CHECK(points[0].position == 0);
    CHECK(tracks.undo());

    CHECK(tracks.removeEnvelopePoint(track, 0));
    CHECK(points.size() == 1);
    CHECK(tracks.undo());
    CHECK(points.size() == 2);

    // Bypass is undoable and reaches the engine snapshot.
    CHECK(tracks.playbackSnapshot()->tracks[0].envelope.active());
    CHECK(tracks.setEnvelopeBypassed(track, true));
    CHECK(role(tracks, 0, TrackListWrapper::EnvelopeBypassedRole).toBool());
    CHECK(!tracks.playbackSnapshot()->tracks[0].envelope.active());
    CHECK(tracks.undo());
    CHECK(!role(tracks, 0, TrackListWrapper::EnvelopeBypassedRole).toBool());

    // The readout of the fader-shaped scale.
    CHECK(tracks.envelopeDbText(0.0) == "-inf dB");
    CHECK(tracks.envelopeDbText(unity) == "0.0 dB");
    CHECK(tracks.envelopeDbText(1.0) == "+6.0 dB");
}

TEST(qtvolumeenvelope, showing_a_lane_changes_the_row_layout_but_not_the_undo_history)
{
    TrackListWrapper tracks;
    const int a = tracks.addAudioTrack(), b = tracks.addAudioTrack();
    tracks.setTrackRowHeight(106);
    tracks.setEnvelopeLaneHeight(72);
    CHECK(tracks.layoutHeight() == 212);
    const bool couldUndo = tracks.canUndo();

    int layout = 0;
    QObject::connect(&tracks, &TrackListWrapper::layoutChanged, [&] { ++layout; });
    CHECK(tracks.toggleEnvelopeShown(a));
    CHECK(role(tracks, 0, TrackListWrapper::EnvelopeShownRole).toBool());
    CHECK(layout == 1);
    CHECK(tracks.layoutHeight() == 284);
    CHECK(tracks.rowTops()[1].toDouble() == 178);
    CHECK(tracks.rowAtY(150) == 0 && tracks.inEnvelopeLane(150));
    CHECK(tracks.rowAtY(180) == 1 && !tracks.inEnvelopeLane(180));
    CHECK_NEAR(tracks.rowCoordinate(150), 1.0, 1e-12);
    CHECK(tracks.canUndo() == couldUndo);   // view state: no undo step
    // Showing it gave the empty envelope its start point, and the views hear about it.
    const auto start = role(tracks, 0, TrackListWrapper::EnvelopePointsRole).toList();
    CHECK(start.size() == 1);
    CHECK_NEAR(start[0].toMap()["beat"].toDouble(), 0.0, 1e-12);
    CHECK_NEAR(start[0].toMap()["level"].toDouble(), tracks.envelopeUnityLevel(), 1e-12);

    // Over the clips: no lane of its own.
    CHECK(tracks.setEnvelopeOverClips(a, true));
    CHECK(tracks.layoutHeight() == 212);
    CHECK(tracks.setEnvelopeOverClips(a, false));
    CHECK(tracks.layoutHeight() == 284);

    // The lane goes with its track when rows move.
    CHECK(tracks.moveTrack(a, 1));
    CHECK(tracks.rowTops()[1].toDouble() == 106 && tracks.layoutHeight() == 284);
    CHECK(tracks.trackIdAt(0) == b);

    CHECK(tracks.toggleEnvelopeShown(a));
    CHECK(tracks.layoutHeight() == 212);
    CHECK(tracks.removeTrack(b));
    CHECK(tracks.layoutHeight() == 106);
}

TEST(qtvolumeenvelope, envelope_beats_follow_a_tempo_change)
{
    TrackListWrapper tracks;
    const int track = tracks.addAudioTrack();
    CHECK(tracks.addEnvelopePoint(track, 4.0, 0.5) == 0);
    tracks.setTempo(60.0, 4, 4);
    const auto list = tracks.data(tracks.index(0), TrackListWrapper::EnvelopePointsRole).toList();
    CHECK_NEAR(list[0].toMap()["beat"].toDouble(), 2.0, 1e-9);   // same sample, half the tempo
    CHECK(tracks.project().findTrack(track)->volumeEnvelope.points()[0].position == 96000);
}

TEST(qtvolumeenvelope, curve_shapes_are_chosen_by_id_and_the_drawn_curve_is_the_played_one)
{
    TrackListWrapper tracks;
    const int track = tracks.addAudioTrack();
    const auto shapes = tracks.envelopeShapes();
    CHECK(shapes.size() == 5);
    QStringList ids, names;
    for (const auto& s : shapes) { ids << s.toMap()["id"].toString(); names << s.toMap()["name"].toString(); }
    CHECK(ids == QStringList({"linear", "smooth", "fastStart", "fastEnd", "hold"}));
    CHECK(names == QStringList({"Linear", "Smooth", "Fast start", "Fast end", "Hold"}));

    CHECK(tracks.addEnvelopePoint(track, 0.0, 0.2) == 0);
    CHECK(tracks.addEnvelopePoint(track, 4.0, 0.9) == 1);
    CHECK(tracks.data(tracks.index(0), TrackListWrapper::EnvelopePointsRole).toList()[0].toMap()["shape"].toString() == "linear");
    CHECK(tracks.setEnvelopePointShape(track, 0, "hold"));
    CHECK(!tracks.setEnvelopePointShape(track, 0, "bezier"));
    CHECK(!tracks.setEnvelopePointShape(track, 9, "smooth"));
    CHECK(tracks.data(tracks.index(0), TrackListWrapper::EnvelopePointsRole).toList()[0].toMap()["shape"].toString() == "hold");

    // Hold: flat until the next point, then the jump, exactly at the point.
    CHECK_NEAR(tracks.envelopeLevelAt(track, 3.99), 0.2, 1e-12);
    CHECK_NEAR(tracks.envelopeLevelAt(track, 4.0), 0.9, 1e-12);
    const auto curve = tracks.envelopeCurve(track, 0.0, 8.0, 16);
    CHECK(curve.size() >= 17);
    double last = -1;
    bool jump = false;
    for (int i = 0; i < curve.size(); ++i) {
        const QPointF p = curve[i].toPointF();
        CHECK(p.x() >= last);
        last = p.x();
        CHECK_NEAR(p.y(), tracks.envelopeLevelAt(track, p.x()), 1e-9);
        if (i > 0 && std::abs(p.x() - curve[i - 1].toPointF().x()) < 1e-3 && p.y() - curve[i - 1].toPointF().y() > 0.6) jump = true;
    }
    CHECK(jump);   // the sample just before the point and the point itself: a vertical step

    CHECK(tracks.setEnvelopePointShape(track, 0, "smooth"));
    const double quarter = tracks.envelopeLevelAt(track, 1.0);
    CHECK(quarter < 0.2 + 0.7 * 0.25);   // eases out of the first point
    CHECK(tracks.undo());
    CHECK(tracks.data(tracks.index(0), TrackListWrapper::EnvelopePointsRole).toList()[0].toMap()["shape"].toString() == "hold");
    CHECK(tracks.envelopeCurve(track, 2.0, 1.0, 4).isEmpty());
    CHECK(tracks.envelopeCurve(-5, 0.0, 1.0, 4).isEmpty());
}
