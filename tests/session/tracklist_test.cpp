#include "testkit.h"
#include "session/tracklist.h"

#include <limits>

TEST(tracklist, catalog_creation_and_unicode_name_colour_rules_preserve_identity_and_history)
{
    TrackList tracks;
    const int vocal = tracks.addTrackChoice("Vocal"), guitar = tracks.addTrackChoice("Electric Guitar");
    CHECK(vocal > 0 && guitar > vocal);
    CHECK(tracks.addTrackChoice("Imaginary") == -1);
    CHECK(tracks.project().findTrack(guitar)->name == "Electric Guitar 1");
    CHECK(tracks.addInstrumentTrackOfType("Electric Guitar") > guitar);
    CHECK(tracks.project().tracks().back()->name == "Electric Guitar 2");
    const int clip = tracks.addClipAtBeat(vocal, 1, 4);
    CHECK(tracks.renameTrack(vocal, "  Lead voix — रात 🎤  ").changed);
    CHECK(tracks.project().findTrack(vocal)->name == "Lead voix — रात 🎤");
    CHECK(!tracks.renameTrack(vocal, "Lead voix — रात 🎤").changed);
    for (const auto& name : {std::string(), std::string("  "), std::string("two\nlines"), std::string(257, 'x')})
        CHECK(!tracks.renameTrack(vocal, name).accepted);
    CHECK(tracks.setTrackColour(vocal, "  #A1B2C3 ").accepted);
    CHECK(tracks.project().findTrack(vocal)->colour == "#a1b2c3");
    CHECK(!tracks.setTrackColour(vocal, "#zz0011").accepted);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(vocal)->audioClips.empty());
    CHECK(tracks.redo()); CHECK(tracks.project().findTrack(vocal)->audioClips[0].id == clip);
    CHECK(tracks.project().findTrack(vocal)->type == "Vocal");
}

TEST(tracklist, media_rate_trims_splits_and_group_moves_preserve_samples_and_atomic_history)
{
    TrackList tracks;
    const int a = tracks.addAudioTrack(), b = tracks.addAudioTrack(), c = tracks.addVocalTrack();
    auto audio = std::make_shared<AudioData>(); audio->path = "source.wav"; audio->sampleRate = 24000; audio->frames = 48000;
    audio->channels = {std::vector<float>(48000, 0.2f)};
    CHECK(tracks.attachTake(a, 0, audio, false));
    const int left = tracks.project().findTrack(a)->audioClips[0].id;
    const int right = tracks.splitClipAtBeat(a, left, 2);
    CHECK(right > left);
    CHECK(tracks.findClip(a, right)->sourceOffset == 24000);
    CHECK(tracks.trimClipStartAtBeat(a, right, 3));
    CHECK(tracks.findClip(a, right)->sourceOffset == 36000);
    CHECK(tracks.undo());
    CHECK(tracks.mergeClips(a, left, right));
    CHECK(tracks.findClip(a, left)->length == 96000);
    CHECK(tracks.undo());
    const int other = tracks.addClipAtBeat(b, 4, 2);
    CHECK(tracks.editClips({right, other}, "move", 1, 1));
    CHECK(!tracks.findClip(a, right));
    CHECK(tracks.findClip(b, right)->sourceOffset == 24000);
    CHECK(!tracks.findClip(b, right)->effectsPrinted);
    CHECK(tracks.findClip(c, other));
    CHECK(tracks.undo());
    CHECK(tracks.findClip(a, right)); CHECK(tracks.findClip(b, other));
    CHECK(!tracks.findClip(c, other));
    CHECK(tracks.audioFor("source.wav").get() == audio.get());
}

TEST(tracklist, invalid_group_requests_and_recording_locks_leave_document_and_history_unchanged)
{
    TrackList tracks;
    const int a = tracks.addAudioTrack(), b = tracks.addAudioTrack();
    const int x = tracks.addClipAtBeat(a, 2, 4), y = tracks.addClipAtBeat(b, 3, 4);
    tracks.project().clearHistory();
    const auto noop = tracks.editClips({x}, "move", 0);
    CHECK(noop.accepted && !noop.changed);
    CHECK(!tracks.editClips({x, 99999}, "delete"));
    CHECK(!tracks.editClips({x, y}, "move", -3));
    CHECK(!tracks.editClips({x, y}, "move", 0, 1));
    CHECK(!tracks.editClips({x, x}, "delete"));
    CHECK(!tracks.editClips({x}, "move", std::numeric_limits<double>::infinity()));
    CHECK(!tracks.canUndo());
    CHECK(tracks.findClip(a, x)->start == 48000);
    CHECK(tracks.findClip(b, y)->start == 72000);
    tracks.lockRecordingTrack(a);
    CHECK(!tracks.editClips({x}, "move", 1));
    CHECK(!tracks.removeAudioClip(a, x));
    CHECK(!tracks.removeTrack(a));
    CHECK(!tracks.setTrackInputPort(a, "new-port").accepted);
    CHECK(!tracks.setRecordDry(a, false).accepted);
    CHECK(tracks.renameTrack(a, "Recording").accepted);
    tracks.lockRecordingTrack(-1);
    CHECK(tracks.editClips({x, y}, "move", 1));
    CHECK(tracks.undo()); CHECK(tracks.redo());
}

TEST(tracklist, monitored_track_gain_and_capabilities_come_from_shared_backend_state)
{
    TrackList tracks;
    const int vocal = tracks.addVocalTrack(), electric = tracks.addInstrumentTrackOfType("Electric Guitar");
    const int strings = tracks.addInstrumentTrackOfType("Strings");
    CHECK(tracks.project().findTrack(electric)->hasLiveInput());
    CHECK(!tracks.project().findTrack(strings)->hasLiveInput());
    CHECK(!tracks.setMonitorEnabled(strings, true).accepted);
    CHECK(tracks.setMonitorEnabled(vocal, true).accepted);
    CHECK(tracks.setVolume(vocal, 0.5).accepted);
    CHECK(tracks.setTrackInputPort(vocal, "mic").accepted);
    CHECK(tracks.cleanMonitor().input == "mic");
    CHECK_NEAR(tracks.cleanMonitor().gain, 0.5, 1e-12);
    CHECK(tracks.setSolo(electric, true).accepted);
    CHECK(tracks.cleanMonitor().gain == 0);
    CHECK(tracks.setSolo(vocal, true).accepted);
    CHECK_NEAR(tracks.cleanMonitor().gain, 0.5, 1e-12);
}

TEST(tracklist, extreme_clip_edits_and_invalid_media_rates_are_rejected_before_arithmetic)
{
    TrackList tracks; const int id = tracks.addAudioTrack();
    const int clip = tracks.addAudioClip(id, "offline", INT64_MAX - 1024, 512);
    tracks.project().clearHistory();
    CHECK(!tracks.editClips({clip}, "move", 1000000000));
    CHECK(tracks.findClip(id, clip)->start == INT64_MAX - 1024);
    CHECK(!tracks.canUndo());
    CHECK(tracks.addClipAtBeat(id, std::ldexp(1.0, 63) / 24000, 4) == -1);
    CHECK(!tracks.resizeClipAtBeat(id, clip, 1e300));
    auto audio = std::make_shared<AudioData>(); audio->path = "invalid.wav"; audio->frames = 1024;
    audio->sampleRate = 0;
    CHECK(!tracks.attachTake(id, 0, audio, true));
    CHECK(!tracks.audioFor("invalid.wav"));
}

TEST(tracklist, track_selection_lifecycle_and_removal_adjustment)
{
    TrackList tracks;
    CHECK(tracks.selectedTrackId() == -1);

    const int t1 = tracks.addAudioTrack();
    CHECK(tracks.selectedTrackId() == t1);

    const int t2 = tracks.addAudioTrack();
    const int t3 = tracks.addVocalTrack();
    CHECK(tracks.selectedTrackId() == t1); // remains t1

    // Select t2
    CHECK(tracks.selectTrack(t2));
    CHECK(tracks.selectedTrackId() == t2);

    // Reject nonexistent track
    CHECK(!tracks.selectTrack(9999));
    CHECK(tracks.selectedTrackId() == t2);

    // Deselect
    CHECK(tracks.selectTrack(-1));
    CHECK(tracks.selectedTrackId() == -1);

    // Reselect t2 and remove it -> selection moves to adjacent track
    CHECK(tracks.selectTrack(t2));
    CHECK(tracks.removeTrack(t2));
    CHECK(tracks.selectedTrackId() == t3);

    // Remove remaining tracks
    CHECK(tracks.removeTrack(t3));
    CHECK(tracks.selectedTrackId() == t1);
    CHECK(tracks.removeTrack(t1));
    CHECK(tracks.selectedTrackId() == -1);
}

TEST(tracklist, clip_gain_operations_and_clip_info)
{
    TrackList tracks;
    const int t = tracks.addAudioTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    CHECK(c > 0);

    const auto clipsInitial = tracks.clips();
    CHECK(clipsInitial.size() == 1);
    CHECK_NEAR(clipsInitial[0].gainDb, 0.0, 1e-6);

    // Set clip gain
    const auto change = tracks.setClipGain(t, c, 4.5);
    CHECK(change.accepted);
    CHECK(change.changed);

    const auto clipsUpdated = tracks.clips();
    CHECK_NEAR(clipsUpdated[0].gainDb, 4.5, 1e-6);
    CHECK_NEAR(tracks.findClip(t, c)->gainDb, 4.5, 1e-6);

    // Setting the same gain returns accepted=true, changed=false
    const auto noChange = tracks.setClipGain(t, c, 4.5);
    CHECK(noChange.accepted);
    CHECK(!noChange.changed);

    // Clamping checks
    CHECK(tracks.setClipGain(t, c, 50.0).accepted);
    CHECK_NEAR(tracks.findClip(t, c)->gainDb, 24.0, 1e-6);

    CHECK(tracks.setClipGain(t, c, -200.0).accepted);
    CHECK_NEAR(tracks.findClip(t, c)->gainDb, -120.0, 1e-6);

    // Invalid values rejected
    CHECK(!tracks.setClipGain(t, c, std::numeric_limits<double>::quiet_NaN()).accepted);
    CHECK(!tracks.setClipGain(999, c, 3.0).accepted);
    CHECK(!tracks.setClipGain(t, 999, 3.0).accepted);

    // Undo / Redo
    CHECK(tracks.undo());
    CHECK_NEAR(tracks.findClip(t, c)->gainDb, 24.0, 1e-6);
    CHECK(tracks.redo());
    CHECK_NEAR(tracks.findClip(t, c)->gainDb, -120.0, 1e-6);

    // Selection tracking
    CHECK(tracks.selectClip(t, c));
    CHECK(tracks.selectedClipId() == c);
    CHECK(tracks.selectedTrackId() == t);
}

TEST(tracklist, master_volume_and_mute)
{
    TrackList tracks;
    CHECK_NEAR(tracks.masterVolume(), 1.0, 1e-6);
    CHECK(!tracks.masterMuted());

    CHECK(tracks.setMasterVolume(1.5).changed);
    CHECK_NEAR(tracks.masterVolume(), 1.5, 1e-6);

    // Clamping
    CHECK(tracks.setMasterVolume(3.0).changed);
    CHECK_NEAR(tracks.masterVolume(), 2.0, 1e-6);
    CHECK(tracks.setMasterVolume(-0.5).changed);
    CHECK_NEAR(tracks.masterVolume(), 0.0, 1e-6);

    // No-change check
    CHECK(!tracks.setMasterVolume(0.0).changed);

    // Master mute
    CHECK(tracks.setMasterMuted(true).changed);
    CHECK(tracks.masterMuted());
    CHECK(!tracks.setMasterMuted(true).changed);
    CHECK(tracks.setMasterMuted(false).changed);
    CHECK(!tracks.masterMuted());
}

TEST(tracklist, a_clip_gain_drag_is_one_undo_step)
{
    TrackList tracks;
    const int t = tracks.addAudioTrack();
    const int a = tracks.addClipAtBeat(t, 0, 4);
    const int b = tracks.addClipAtBeat(t, 8, 4);
    CHECK(a > 0 && b > 0);

    // One drag: first step starts the undo step, later steps extend it
    CHECK(tracks.setClipGain(t, a, -1.0).changed);
    for (double g : {-2.0, -3.0, -6.0}) CHECK(tracks.setClipGain(t, a, g, true).changed);
    CHECK_NEAR(tracks.findClip(t, a)->gainDb, -6.0, 1e-6);
    CHECK(tracks.undo());                                     // one undo undoes the whole drag
    CHECK_NEAR(tracks.findClip(t, a)->gainDb, 0.0, 1e-6);
    CHECK(tracks.redo());
    CHECK_NEAR(tracks.findClip(t, a)->gainDb, -6.0, 1e-6);

    // A new drag (continueEdit=false) is its own step
    CHECK(tracks.setClipGain(t, a, -9.0).changed);
    CHECK(tracks.setClipGain(t, a, -12.0, true).changed);
    CHECK(tracks.undo());
    CHECK_NEAR(tracks.findClip(t, a)->gainDb, -6.0, 1e-6);

    // "Continuing" on a different clip never joins another clip's step
    CHECK(tracks.setClipGain(t, b, 3.0, true).changed);
    CHECK(tracks.undo());
    CHECK_NEAR(tracks.findClip(t, b)->gainDb, 0.0, 1e-6);
    CHECK_NEAR(tracks.findClip(t, a)->gainDb, -6.0, 1e-6);

    // After an undo, continuing does not rewrite the undone step: it adds a new one
    CHECK(tracks.setClipGain(t, a, -20.0, true).changed);
    CHECK(tracks.undo());
    CHECK_NEAR(tracks.findClip(t, a)->gainDb, -6.0, 1e-6);

    // A different kind of edit in between ends the merging
    CHECK(tracks.setClipGain(t, a, -1.0).changed);
    CHECK(tracks.moveClipAtBeat(t, b, 16.0));
    CHECK(tracks.setClipGain(t, a, -2.0, true).changed);
    CHECK(tracks.undo());                                     // undoes only the -2 step
    CHECK_NEAR(tracks.findClip(t, a)->gainDb, -1.0, 1e-6);
}
