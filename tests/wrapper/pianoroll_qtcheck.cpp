#include "testkit.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "wrapper/pianorollwrap/pianorollwrapper.h"


namespace {
PianoRollWrapper* boundRoll(TrackListWrapper& tracks, int track, int clip, QObject* parent)
{
    auto* roll = new PianoRollWrapper(parent);
    roll->setTrackList(&tracks);
    roll->setTrackId(track);
    roll->setClipId(clip);
    return roll;
}
}

TEST(qtpianoroll, edits_are_written_to_the_clip_and_exposed_to_the_timeline) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 4, 4);
    auto* roll = boundRoll(tracks, t, c, &owner);
    CHECK(roll->bound());
    CHECK(roll->count() == 0);

    int clipsChanges = 0;
    QObject::connect(&tracks, &TrackListWrapper::clipsChanged, &owner, [&] { ++clipsChanges; });
    const int id = roll->addNote(60, 1.0, 0.5, 90);
    CHECK(id > 0);
    CHECK(clipsChanges == 1);
    CHECK(tracks.domain().clipNotes(t, c).size() == 1);

    // Timeline sees the notes through clipMap and clipNotes()
    const auto clip = tracks.clips().at(0).toMap();
    const auto timelineNotes = clip.value("notes").toList();
    CHECK(timelineNotes.size() == 1);
    CHECK(timelineNotes[0].toMap().value("note").toInt() == 60);
    CHECK(timelineNotes[0].toMap().value("startBeat").toDouble() == 1.0);
    CHECK(tracks.clipNotes(t, c).size() == 1);

    CHECK(roll->moveNote(id, 64, 2.0));
    CHECK(roll->resizeNote(id, 1.5));
    const auto stored = tracks.domain().clipNotes(t, c);
    CHECK(stored[0].note == 64 && stored[0].startBeat == 2.0 && stored[0].lengthBeats == 1.5);

    // Selection is editor state: it does not touch the document or history
    const int before = clipsChanges;
    roll->selectNote(id);
    CHECK(clipsChanges == before);
    CHECK(roll->selectedCount() == 1);
    CHECK(!tracks.domain().clipNotes(t, c)[0].selected);
}

TEST(qtpianoroll, undo_redo_reload_notes_and_keep_selection) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    auto* roll = boundRoll(tracks, t, c, &owner);
    const int a = roll->addNote(60, 0.0, 1.0);
    const int b = roll->addNote(62, 1.0, 1.0);
    roll->selectNote(a);
    CHECK(roll->moveNote(b, 65, 2.0));

    CHECK(tracks.undo()); // undo the move
    CHECK(roll->backend().findNote(b)->note == 62);
    CHECK(roll->backend().findNote(a)->selected); // selection survives the reload
    CHECK(tracks.undo()); // undo adding b
    CHECK(roll->count() == 1);
    CHECK(tracks.redo());
    CHECK(roll->count() == 2);
}

TEST(qtpianoroll, docked_and_floating_views_stay_in_sync) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    auto* docked = boundRoll(tracks, t, c, &owner);
    auto* floating = boundRoll(tracks, t, c, &owner);
    const int id = docked->addNote(67, 0.5, 0.5);
    CHECK(floating->count() == 1);
    CHECK(floating->moveNote(id, 69, 1.0));
    CHECK(docked->backend().findNote(id)->note == 69);
    CHECK(floating->deleteSelected() == false); // nothing selected there
    floating->selectAll();
    CHECK(floating->deleteSelected());
    CHECK(docked->count() == 0);
}

TEST(qtpianoroll, rejected_edits_revert_and_switching_or_removing_clips_reloads) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int first = tracks.addClipAtBeat(t, 0, 4);
    const int second = tracks.addClipAtBeat(t, 8, 4);
    auto* roll = boundRoll(tracks, t, first, &owner);
    roll->addNote(60, 0.0, 1.0);

    // Recording lock: the edit is rejected and the view shows the document again
    tracks.lockRecordingTrack(t);
    CHECK(roll->addNote(72, 2.0, 1.0) == -1);
    CHECK(roll->count() == 1);
    tracks.lockRecordingTrack(-1);

    // Switching clip loads that clip's (empty) notes; switching back restores them
    roll->setClipId(second);
    CHECK(roll->count() == 0);
    roll->setClipId(first);
    CHECK(roll->count() == 1);

    // Removing the clip empties the view and unbinds it
    CHECK(tracks.removeAudioClip(t, first));
    CHECK(!roll->bound());
    CHECK(roll->count() == 0);
    CHECK(roll->addNote(60, 0.0, 1.0) == -1);

    // Unbound wrapper (no track list) still edits locally
    PianoRollWrapper local;
    CHECK(local.addNote(60, 0.0, 1.0) > 0);
    CHECK(local.count() == 1);
}

TEST(qtpianoroll, clip_start_beat_is_exposed_and_notifies_on_clip_moves) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 4, 4);
    auto* roll = boundRoll(tracks, t, c, &owner);
    CHECK(std::abs(roll->clipStartBeat() - 4.0) < 1e-6);

    int startChanges = 0;
    QObject::connect(roll, &PianoRollWrapper::clipStartChanged, &owner, [&] { ++startChanges; });
    CHECK(tracks.moveClipAtBeat(t, c, 8.0)); // notes unchanged, start moved
    CHECK(startChanges >= 1);
    CHECK(std::abs(roll->clipStartBeat() - 8.0) < 1e-6);

    CHECK(tracks.removeAudioClip(t, c)); // unbound: no start
    CHECK(roll->clipStartBeat() == 0.0);
}

TEST(qtpianoroll, meter_follows_the_project_time_signature) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    auto* roll = boundRoll(tracks, t, c, &owner);
    CHECK(roll->beatsPerBar() == 4.0 && roll->beatUnit() == 1.0);
    CHECK(roll->positionLabel(5.25) == "2.2.25");

    int meterChanges = 0;
    QObject::connect(roll, &PianoRollWrapper::meterChanged, &owner, [&] { ++meterChanges; });
    const double bpm = tracks.domain().project().tempo().bpm();
    tracks.setTempo(bpm, 6, 8);
    CHECK(meterChanges >= 1);
    CHECK(roll->beatsPerBar() == 3.0 && roll->beatUnit() == 0.5);
    CHECK(roll->positionLabel(3.75) == "2.2.50");

    PianoRollWrapper unbound; // no project: plain 4/4
    CHECK(unbound.beatsPerBar() == 4.0 && unbound.beatUnit() == 1.0);
}

TEST(qtpianoroll, quantize_is_one_undoable_edit) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    auto* roll = boundRoll(tracks, t, c, &owner);
    const int a = roll->addNote(60, 0.3, 0.5);
    const int b = roll->addNote(64, 1.13, 0.5);

    int clipsChanges = 0;
    QObject::connect(&tracks, &TrackListWrapper::clipsChanged, &owner, [&] { ++clipsChanges; });
    CHECK(roll->quantize(0.25));
    CHECK(clipsChanges == 1);                                  // one document edit
    const auto stored = tracks.domain().clipNotes(t, c);
    CHECK(stored[0].startBeat == 0.25 && stored[1].startBeat == 1.25);
    CHECK(!roll->quantize(0.25));                              // already on the grid
    CHECK(clipsChanges == 1);

    CHECK(tracks.undo());                                      // single undo restores both
    CHECK(roll->backend().findNote(a)->startBeat == 0.3);
    CHECK(roll->backend().findNote(b)->startBeat == 1.13);
}

TEST(qtpianoroll, track_colour_follows_the_bound_track) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    auto* roll = boundRoll(tracks, t, c, &owner);
    CHECK(roll->trackColour() == QString::fromStdString(tracks.domain().project().findTrack(t)->colour));
    int changes = 0;
    QObject::connect(roll, &PianoRollWrapper::trackColourChanged, &owner, [&] { ++changes; });
    CHECK(tracks.setTrackColour(t, "#d97c73"));
    CHECK(changes >= 1);
    CHECK(roll->trackColour() == "#d97c73");
    PianoRollWrapper unbound;
    CHECK(unbound.trackColour().isEmpty());
}

TEST(qtpianoroll, grid_settings_come_from_the_backend_and_follow_the_meter) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    auto* roll = boundRoll(tracks, t, c, &owner);
    CHECK(roll->gridDivisionOptions().size() == 6 && roll->gridStyleOptions().size() == 3);
    CHECK(roll->gridDivision() == "1/16" && roll->snapEnabled());
    CHECK(std::abs(roll->snapFloor(1.3) - 1.25) < 1e-9);

    int changes = 0;
    QObject::connect(roll, &PianoRollWrapper::gridChanged, &owner, [&] { ++changes; });
    roll->setGridStyle("dotted");
    CHECK(changes == 1 && roll->gridStyle() == "dotted");
    roll->setGridStyle("swing");                               // unknown name: ignored
    CHECK(changes == 1 && roll->gridStyle() == "dotted");
    roll->setSnapEnabled(false);
    CHECK(changes == 2 && roll->snapStep() == 0.0);
    roll->setPixelsPerBeat(16);
    CHECK(changes == 3);

    // The project meter reaches the grid: 6/8 bars are 3 quarters long
    const double bpm = tracks.domain().project().tempo().bpm();
    roll->setSnapEnabled(true);
    roll->setGridStyle("straight");
    tracks.setTempo(bpm, 6, 8);
    CHECK(roll->grid().beatsPerBar() == 3.0 && roll->grid().beatUnit() == 0.5);
    CHECK(roll->gridLinesPerBar() == 6);                       // 1/16 at 16 px -> eighth lines

    // quantizeToGrid uses the Grid division, as one undoable edit
    roll->setGridDivision("1/8");
    const int id = roll->addNote(60, 0.3, 0.5);
    CHECK(roll->quantizeToGrid());
    CHECK(std::abs(roll->backend().findNote(id)->startBeat - 0.5) < 1e-9);
}

TEST(qtpianoroll, note_names_come_from_the_backend) {
    PianoRollWrapper roll;
    CHECK(roll.noteName(60) == "C4" && roll.noteName(70) == "A#4");
    CHECK(roll.noteReadout(64) == "E4 64");
    CHECK(roll.isBlackKey(63) && !roll.isBlackKey(64));
    CHECK(roll.isOctaveStart(72) && !roll.isOctaveStart(73));
    CHECK(roll.noteName(-3).isEmpty());
}

TEST(qtpianoroll, velocity_edits_are_one_undoable_change_and_clamped) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    auto* roll = boundRoll(tracks, t, c, &owner);
    const int a = roll->addNote(60, 0.0, 1.0, 100);
    const int b = roll->addNote(64, 1.0, 1.0, 100);

    int clipsChanges = 0;
    QObject::connect(&tracks, &TrackListWrapper::clipsChanged, &owner, [&] { ++clipsChanges; });
    CHECK(roll->setNoteVelocities({a, b}, {40, 500}));
    CHECK(clipsChanges == 1);                                  // one document edit
    auto stored = tracks.domain().clipNotes(t, c);
    CHECK(stored[0].velocity == 40 && stored[1].velocity == 127); // clamped
    CHECK(tracks.clips().at(0).toMap().value("notes").toList()[0].toMap().value("velocity").toInt() == 40);

    CHECK(roll->setNoteVelocities({a}, {40}));                 // no change -> no edit
    CHECK(clipsChanges == 1);
    CHECK(!roll->setNoteVelocities({a, b}, {10}));             // mismatched lists rejected
    CHECK(roll->setNoteVelocities({9999}, {10}));              // unknown ids are ignored

    CHECK(tracks.undo());                                      // single undo restores both
    CHECK(roll->backend().findNote(a)->velocity == 100);
    CHECK(roll->backend().findNote(b)->velocity == 100);

    tracks.lockRecordingTrack(t);                              // rejected while recording -> reverted
    CHECK(!roll->setNoteVelocities({a}, {20}));
    CHECK(roll->backend().findNote(a)->velocity == 100);
}

TEST(qtpianoroll, clip_length_and_new_note_velocity_follow_the_clip_and_the_edits) {
    QObject owner;
    TrackListWrapper tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 4, 4);
    auto* roll = boundRoll(tracks, t, c, &owner);
    CHECK(std::abs(roll->clipLengthBeats() - 4.0) < 1e-6);
    int changes = 0;
    QObject::connect(roll, &PianoRollWrapper::clipStartChanged, &owner, [&] { ++changes; });
    CHECK(tracks.resizeClipAtBeat(t, c, 6.0));                 // a timeline resize
    CHECK(changes >= 1 && std::abs(roll->clipLengthBeats() - 6.0) < 1e-6);
    CHECK(tracks.undo());
    CHECK(std::abs(roll->clipLengthBeats() - 4.0) < 1e-6);

    CHECK(roll->newNoteVelocity() == 100);
    const int n = roll->addNote(60, 0, 1, 100);
    CHECK(roll->setNoteVelocities({n}, {42}));
    CHECK(roll->newNoteVelocity() == 42);
    const int m = roll->addNote(62, 1, 1, 77);
    roll->selectNote(m);
    CHECK(roll->newNoteVelocity() == 77);

    CHECK(tracks.removeAudioClip(t, c));                       // unbound: no length
    CHECK(roll->clipLengthBeats() == 0.0);
}
