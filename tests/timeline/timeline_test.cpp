#include "testkit.h"
#include "timeline/timeline.h"

TEST(timeline, snap_modes_respect_meter_direction_and_free_nudge_duration)
{
    TrackList tracks; const int track = tracks.addAudioTrack(); tracks.addClipAtBeat(track, 2, 4);
    Timeline editor; editor.setTracks(&tracks); editor.setMeter(3, 8); editor.selectAll();
    editor.setSnapMode(0); CHECK(editor.snapStep() == 1.5);
    CHECK(editor.snap(1.4, "floor") == 0); CHECK(editor.snap(1.4, "ceil") == 1.5);
    editor.setSnapMode(1); CHECK(editor.snapStep() == 0.5);
    editor.setSnapMode(5); CHECK(editor.snapStep() == 0.03125);
    editor.setSnapMode(6); CHECK(editor.snapStep() == 0);
    CHECK(editor.snap(1.234) == 1.234); CHECK(editor.snap(-1) == 0);
    editor.setBpm(97.5);
    CHECK_NEAR(editor.nudge(1)->delta * 60 / 97.5, 0.001, 1e-12);
    CHECK(editor.isBarLine(3)); CHECK(!editor.isBarLine(2)); CHECK(editor.barLabel(99) == "100");
}

TEST(timeline, group_preview_constrains_all_lanes_and_changes_no_document_until_commit)
{
    TrackList tracks; const int a = tracks.addAudioTrack(), b = tracks.addAudioTrack(); tracks.addAudioTrack();
    const int x = tracks.addClipAtBeat(a, 1, 4), y = tracks.addClipAtBeat(b, 2, 2);
    Timeline editor; editor.setTracks(&tracks); editor.selectAll(); editor.beginEdit("move");
    editor.updateEdit(-50); editor.updateLane(50);
    CHECK(editor.editDelta() == -1); CHECK(editor.editLaneDelta() == 1);
    CHECK(tracks.findClip(a, x)->start == 24000);
    const auto previews = editor.previews();
    CHECK(previews[0].start == 0 && previews[0].lane == 1);
    CHECK(previews[1].start == 1 && previews[1].lane == 2);
    const auto request = editor.commitEdit(); CHECK(request.has_value());
    CHECK(tracks.editClips(request->ids, request->operation, request->delta, request->lanes));
    editor.sync(); CHECK(editor.selectedTrackId() == b);
    CHECK(tracks.undo()); editor.sync(); CHECK(tracks.findClip(a, x)); CHECK(tracks.findClip(b, y));
    editor.beginEdit("resize"); editor.updateEdit(-100);
    CHECK_NEAR(editor.editDelta(), 1e-6 - 2, 1e-12);
    editor.cancelEdit();
    CHECK(tracks.removeAudioClip(a, x)); editor.sync();
    CHECK(editor.selectedIds().size() == 1 && editor.selectedIds()[0] == y);
}

TEST(timeline, overlap_selection_repeat_acknowledgement_and_busy_rules_are_backend_owned)
{
    TrackList tracks; const int a = tracks.addAudioTrack(), b = tracks.addVocalTrack();
    const int x = tracks.addClipAtBeat(a, 1, 2), y = tracks.addClipAtBeat(b, 2, 2);
    Timeline editor; editor.setTracks(&tracks);
    CHECK(!editor.createClip(1, 0)); CHECK(editor.createClip(8, 0));
    CHECK(!editor.createClip(8, 5));
    editor.beginSelection(0, 0, true); editor.updateSelection(4, 2, true);
    editor.finishSelection(8.0 / 106, 10.0 / 106, 1.0 / 106);
    CHECK(editor.selectedIds() == std::vector<int>({x, y}));
    editor.beginSelection(1.2, 0, false); editor.updateSelection(3.2, 0, true);
    const auto range = editor.finishSelection(0, 0, 0);
    CHECK(range && range->start == 1 && range->end == 4);
    editor.acceptRepeatRange(false); CHECK(editor.savedRangeEnd() == 0);
    editor.beginSelection(1.2, 0, false); editor.updateSelection(3.2, 0, true);
    editor.finishSelection(0, 0, 0); editor.acceptRepeatRange(true);
    CHECK(editor.savedRangeStart() == 1 && editor.savedRangeEnd() == 4);
    editor.selectAll(); editor.setBusy(true);
    CHECK(!editor.nudge(1)); CHECK(!editor.createClip(8, 0));
    editor.beginEdit("move"); CHECK(editor.editOperation().empty());
    editor.setBusy(false); editor.beginEdit("move"); editor.setProjectLoading(true);
    CHECK(!editor.commitEdit());
    editor.setTracks(nullptr); CHECK(editor.selectedIds().empty()); CHECK(!editor.keyboardAllowed());
}

TEST(timeline, one_sample_low_tempo_clip_does_not_produce_an_inverted_trim_range)
{
    TrackList tracks; const int id = tracks.addAudioTrack();
    tracks.setTempo(0.01, 4, 4);
    tracks.addAudioClip(id, "one-sample", 0, 1);
    Timeline editor; editor.setTracks(&tracks); editor.selectAll(); editor.beginEdit("trimStart");
    editor.updateEdit(1);
    CHECK(editor.editDelta() == 0);
    const auto preview = editor.previews();
    CHECK(preview[0].start == 0 && preview[0].length > 0);
}
