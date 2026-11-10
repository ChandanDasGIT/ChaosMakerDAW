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
    // Every instrument track can monitor: its live input is MIDI. It never takes the audio
    // clean monitor, which follows the tracks with a live audio input.
    CHECK(tracks.project().findTrack(strings)->canMonitor());
    CHECK(tracks.setMonitorEnabled(strings, true).accepted);
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

TEST(tracklist, removing_a_track_can_be_undone_and_redone)
{
    TrackList tracks;
    const int a = tracks.addAudioTrack();
    const int b = tracks.addInstrumentTrack();
    const int c = tracks.addAudioTrack();
    const int clip = tracks.addClipAtBeat(b, 0, 4);
    CHECK(tracks.setClipGain(b, clip, -6.0).changed);            // an older edit on that track
    CHECK(tracks.setTrackColour(b, "#d97c73").changed);
    CHECK(tracks.setEqBand(b, 3, 4.5).changed);
    tracks.selectTrack(b);

    CHECK(tracks.removeTrack(b));
    CHECK(tracks.count() == 2 && !tracks.project().findTrack(b));
    CHECK(tracks.selectedTrackId() == c);                         // neighbour took the row
    CHECK(tracks.canUndo());                                      // history is kept now
    const auto restore = tracks.undoRestoresTrack();
    CHECK(restore && restore->trackId == b && restore->index == 1);

    CHECK(tracks.undo());                                         // track back: same id, row, content
    CHECK(tracks.count() == 3 && tracks.trackIdAt(1) == b);
    const auto* back = tracks.project().findTrack(b);
    CHECK(back && back->audioClips.size() == 1);
    CHECK_NEAR(back->audioClips[0].gainDb, -6.0, 1e-6);
    CHECK(back->colour == "#d97c73");
    CHECK_NEAR(back->eqGainDb[3], 4.5, 1e-6);
    CHECK(tracks.undo());                                         // the older clip edit still undoes
    CHECK_NEAR(tracks.findClip(b, clip)->gainDb, 0.0, 1e-6);

    CHECK(tracks.redo());                                         // gain again
    const auto removal = tracks.redoRemovesTrack();
    CHECK(removal && removal->trackId == b && removal->index == 1);
    tracks.selectTrack(b);
    CHECK(tracks.redo());                                         // removed again
    CHECK(tracks.count() == 2 && !tracks.project().findTrack(b) && tracks.selectedTrackId() == c);
    CHECK(tracks.trackIdAt(0) == a);

    // A new track never reuses the removed id, so undo cannot collide with it
    const int d = tracks.addAudioTrack();
    CHECK(d != b);
}

TEST(tracklist, set_pan_clamps_and_reports_changes)
{
    TrackList tracks;
    const int id = tracks.addAudioTrack();
    CHECK(tracks.project().findTrack(id)->pan == 0.0f);
    const auto changed = tracks.setPan(id, -0.4);
    CHECK(changed.accepted && changed.changed);
    CHECK_NEAR(tracks.project().findTrack(id)->pan, -0.4f, 1e-6);
    const auto same = tracks.setPan(id, -0.4);
    CHECK(same.accepted && !same.changed);
    CHECK(tracks.setPan(id, 3.0).changed);
    CHECK(tracks.project().findTrack(id)->pan == 1.0f);
    CHECK(!tracks.setPan(id, std::numeric_limits<double>::quiet_NaN()).accepted);
    CHECK(!tracks.setPan(id + 100, 0.5).accepted);
}

TEST(tracklist, set_strip_width_clamps_to_the_mixer_limits)
{
    TrackList tracks;
    const int id = tracks.addAudioTrack();
    CHECK(tracks.project().findTrack(id)->stripWidth == 108.0f);
    const auto wider = tracks.setStripWidth(id, 160.0);
    CHECK(wider.accepted && wider.changed);
    CHECK(tracks.project().findTrack(id)->stripWidth == 160.0f);
    CHECK(!tracks.setStripWidth(id, 160.0).changed);
    CHECK(tracks.setStripWidth(id, 10.0).changed);
    CHECK(tracks.project().findTrack(id)->stripWidth == 82.0f);
    CHECK(!tracks.setStripWidth(id, std::numeric_limits<double>::infinity()).accepted);
    CHECK(!tracks.setStripWidth(id + 50, 120.0).accepted);
}

TEST(tracklist, new_tracks_follow_the_default_record_mode_and_loaded_tracks_keep_theirs)
{
    TrackList tracks;
    // No preference set: Project's rule (dry only for plain Audio tracks).
    CHECK(!tracks.project().findTrack(tracks.addVocalTrack())->recordDry);
    CHECK(tracks.project().findTrack(tracks.addAudioTrack())->recordDry);
    // The preference applies to every track the user adds from now on.
    tracks.setNewTrackRecordDry(true);
    const int vocal = tracks.addVocalTrack(), guitar = tracks.addInstrumentTrackOfType("Electric Guitar");
    CHECK(tracks.project().findTrack(vocal)->recordDry && tracks.project().findTrack(guitar)->recordDry);
    tracks.setNewTrackRecordDry(false);
    CHECK(!tracks.project().findTrack(tracks.addAudioTrack())->recordDry);
    CHECK(tracks.project().findTrack(vocal)->recordDry);           // existing tracks keep their mode
    // Opening another document keeps the preference (it is not part of the document).
    tracks.setNewTrackRecordDry(true);
    TrackList other;
    const int saved = other.addVocalTrack();                       // saved in Print mode
    tracks.adoptDocument(std::move(other));
    CHECK(!tracks.project().findTrack(saved)->recordDry);
    CHECK(tracks.project().findTrack(tracks.addVocalTrack())->recordDry);
}

TEST(tracklist, key_shift_is_the_harmonium_scale_changer_and_zero_elsewhere)
{
    TrackList tracks;
    const int harmonium = tracks.addInstrumentTrackOfType("Harmonium");
    const int drums = tracks.addInstrumentTrackOfType("Drums");
    const int vocal = tracks.addTrackChoice("Vocal");
    CHECK(harmonium > 0 && drums > 0 && vocal > 0);
    CHECK(tracks.project().findTrack(harmonium)->keyShift() == 0);          // C plays C
    auto settings = tracks.project().findTrack(harmonium)->harmonium;
    settings.transpose = 3;
    tracks.setHarmonium(harmonium, settings);
    CHECK(tracks.project().findTrack(harmonium)->keyShift() == 3);
    settings.transpose = -5;
    tracks.setHarmonium(harmonium, settings);
    CHECK(tracks.project().findTrack(harmonium)->keyShift() == -5);
    // Other tracks ignore harmonium settings: only the harmonium has a scale changer today.
    auto other = tracks.project().findTrack(drums)->harmonium;
    other.transpose = 4;
    tracks.setHarmonium(drums, other);
    CHECK(tracks.project().findTrack(drums)->keyShift() == 0);
    CHECK(tracks.project().findTrack(vocal)->keyShift() == 0);
}

TEST(tracklist, the_picker_groups_guitars_and_drums_by_kind)
{
    std::vector<std::string> ids, names;
    for (const auto& c : TrackList::instrumentChoices()) { ids.push_back(c.id); names.push_back(c.name); }
    CHECK(names == (std::vector<std::string>{"Piano", "Guitar", "Strings", "Drums", "Harmonium"}));
    CHECK(ids[1] == "group:Guitar" && ids[3] == "group:Drums");
    for (const auto& c : TrackList::instrumentChoices()) CHECK(c.hasChildren == (c.id.rfind("group:", 0) == 0));

    const auto pairs = [](const std::string& group) {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& c : TrackList::variantChoices(group)) { CHECK(!c.hasChildren); out.push_back({c.id, c.name}); }
        return out;
    };
    using P = std::vector<std::pair<std::string, std::string>>;
    CHECK(pairs("group:Guitar") == (P{{"Acoustic Guitar", "Acoustic"}, {"Electric Guitar", "Electric"}}));
    CHECK(pairs("Drums") == (P{{"Drums", "Acoustic"}, {"Electronic Drums", "Electronic"}}));
    CHECK(TrackList::variantChoices("Piano").empty() && TrackList::variantChoices("").empty());
    CHECK(TrackList::variantChoices("group:").empty());

    // A kind adds its own track type, named as before; a group is not a track type.
    TrackList tracks;
    const int kit = tracks.addTrackChoice("Electronic Drums");
    CHECK(kit > 0 && tracks.project().findTrack(kit)->type == "Electronic Drums");
    CHECK(tracks.project().findTrack(kit)->name == "Electronic Drums 1");
    CHECK(tracks.addTrackChoice("group:Drums") == -1 && tracks.addTrackChoice("Guitar") == -1);
}

TEST(tracklist, volume_envelope_edits_are_undoable_and_the_view_state_is_not)
{
    TrackList tracks;
    const int id = tracks.addAudioTrack();
    const auto& envelope = tracks.project().findTrack(id)->volumeEnvelope;
    CHECK(envelope.points().empty() && !envelope.shown());

    CHECK(tracks.addEnvelopePoint(id, 1000, 0.5) == 0);
    // View state, no undo step (the five undos below reach the start); with a point already
    // there, showing adds no start point.
    CHECK(tracks.setEnvelopeShown(id, true).changed);
    CHECK(tracks.setEnvelopeOverClips(id, true).changed);
    CHECK(envelope.points().size() == 1);
    CHECK(tracks.addEnvelopePoint(id, 2000, 0.8) == 1);
    CHECK(tracks.addEnvelopePoint(-5, 0, 0.5) == -1);
    // One drag (many moves of the same point) is one undo step.
    CHECK(tracks.moveEnvelopePoint(id, 0, 1100, 0.4).changed);
    CHECK(tracks.moveEnvelopePoint(id, 0, 1200, 0.3, true).changed);
    CHECK(tracks.moveEnvelopePoint(id, 0, 1300, 0.2, true).changed);
    CHECK(!tracks.moveEnvelopePoint(id, 0, 1300, 0.2, true).changed);
    CHECK(tracks.setEnvelopeBypassed(id, true).changed);
    CHECK(!tracks.setEnvelopeBypassed(id, true).changed);
    CHECK(tracks.removeEnvelopePoint(id, 1).changed);
    CHECK(!tracks.removeEnvelopePoint(id, 5).accepted);

    CHECK(tracks.undo()); CHECK(envelope.points().size() == 2);   // remove
    CHECK(tracks.undo()); CHECK(!envelope.bypassed());            // bypass
    CHECK(tracks.undo());                                         // the whole drag
    CHECK(envelope.points()[0] == (automation::EnvelopePoint{1000, 0.5}));
    CHECK(envelope.shown() && envelope.overClips());              // undo leaves the view alone
    CHECK(tracks.undo()); CHECK(envelope.points().size() == 1);
    CHECK(tracks.undo()); CHECK(envelope.points().empty());
    CHECK(!tracks.canUndo());
    for (int i = 0; i < 4; ++i) CHECK(tracks.redo());
    CHECK(envelope.points()[0] == (automation::EnvelopePoint{1300, 0.2}) && envelope.bypassed());
    CHECK(tracks.redo()); CHECK(envelope.points().size() == 1);

    // Clip edits and envelope edits share one history.
    const int clip = tracks.addClipAtBeat(id, 0, 4);
    CHECK(clip > 0);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(id)->audioClips.empty());
    CHECK(envelope.points().size() == 1);

    // While recording, the undoable edits are refused; the view still switches.
    tracks.lockRecordingTrack(id);
    CHECK(tracks.addEnvelopePoint(id, 5000, 0.5) == -1);
    CHECK(!tracks.moveEnvelopePoint(id, 0, 10, 0.5).accepted);
    CHECK(!tracks.removeEnvelopePoint(id, 0).accepted);
    CHECK(!tracks.setEnvelopeBypassed(id, false).accepted);
    CHECK(tracks.setEnvelopeShown(id, false).changed);
    tracks.lockRecordingTrack(-1);

    // Removing the track keeps its envelope for undo.
    CHECK(tracks.removeTrack(id));
    CHECK(tracks.undo());
    CHECK(tracks.project().findTrack(id)->volumeEnvelope.points().size() == 1);
}

TEST(tracklist, showing_an_empty_envelope_adds_a_start_point_at_zero_db)
{
    TrackList tracks;
    const int id = tracks.addAudioTrack();
    const auto& envelope = tracks.project().findTrack(id)->volumeEnvelope;
    CHECK(tracks.setEnvelopeShown(id, true).changed);
    CHECK(envelope.points().size() == 1);
    CHECK(envelope.points()[0].position == 0);
    CHECK_NEAR(envelope.points()[0].level, automation::envelopescale::unityLevel(), 1e-12);
    CHECK_NEAR(envelope.gainAt(123456), 1.0, 1e-12);   // no change in sound
    CHECK(!tracks.canUndo());                          // part of the view change

    // An ordinary point: it can be moved and deleted.
    CHECK(tracks.moveEnvelopePoint(id, 0, 500, 0.5).changed);
    CHECK(tracks.removeEnvelopePoint(id, 0).changed);
    CHECK(envelope.points().empty());
    // Hiding and showing again gives a new start point; hiding alone adds nothing.
    CHECK(tracks.setEnvelopeShown(id, false).changed);
    CHECK(envelope.points().empty());
    CHECK(tracks.setEnvelopeShown(id, true).changed);
    CHECK(envelope.points().size() == 1 && envelope.points()[0].position == 0);

    // With points already there, nothing is added.
    const int other = tracks.addAudioTrack();
    CHECK(tracks.addEnvelopePoint(other, 48000, 0.3) == 0);
    CHECK(tracks.setEnvelopeShown(other, true).changed);
    const auto& otherEnvelope = tracks.project().findTrack(other)->volumeEnvelope;
    CHECK(otherEnvelope.points().size() == 1 && otherEnvelope.points()[0].position == 48000);
}

TEST(tracklist, an_envelope_points_curve_shape_is_one_undo_step)
{
    using automation::CurveShape;
    TrackList tracks;
    const int id = tracks.addAudioTrack();
    CHECK(tracks.addEnvelopePoint(id, 0, 0.2) == 0);
    CHECK(tracks.addEnvelopePoint(id, 48000, 0.9) == 1);
    const auto& envelope = tracks.project().findTrack(id)->volumeEnvelope;

    CHECK(tracks.setEnvelopePointShape(id, 0, CurveShape::Smooth).changed);
    CHECK(!tracks.setEnvelopePointShape(id, 0, CurveShape::Smooth).changed);   // same shape: no step
    CHECK(!tracks.setEnvelopePointShape(id, 7, CurveShape::Hold).accepted);
    CHECK(!tracks.setEnvelopePointShape(-3, 0, CurveShape::Hold).accepted);
    CHECK(tracks.envelopePointBeats(id)[0].shape == CurveShape::Smooth);
    CHECK(tracks.undo());
    CHECK(envelope.points()[0].shape == CurveShape::Linear && envelope.points().size() == 2);
    CHECK(tracks.redo());
    CHECK(envelope.points()[0].shape == CurveShape::Smooth);

    tracks.lockRecordingTrack(id);   // refused while recording, like the other envelope edits
    CHECK(!tracks.setEnvelopePointShape(id, 0, CurveShape::Hold).accepted);
    tracks.lockRecordingTrack(-1);
    CHECK(envelope.points()[0].shape == CurveShape::Smooth);
}

TEST(tracklist, the_master_track_takes_effects_but_is_never_a_track)
{
    TrackList tracks;
    const int vocal = tracks.addVocalTrack();
    const int master = Project::MasterTrackId;
    CHECK(tracks.project().findTrack(master) == &tracks.project().master());
    CHECK(tracks.project().tracks().size() == 1);               // not in the list
    CHECK(tracks.project().master().effects.empty());
    const int limiter = tracks.addTrackEffect(master, "limiter");
    CHECK(limiter > 0);
    CHECK(tracks.addTrackEffect(master, "chaoseq") > 0);
    CHECK(tracks.addTrackEffect(master, "guitarsuite") < 0);   // instrument pages are not offered
    CHECK(tracks.moveTrackEffect(master, limiter, 1).accepted);
    CHECK(tracks.project().master().effects[1].type == "limiter");
    auto settings = *tracks.mixEffect(master, "limiter");
    settings.values["ceiling"] = -3;
    CHECK(tracks.setMixEffect(master, "limiter", settings).changed);
    CHECK(tracks.setEqBand(master, 3, 2.0).accepted);
    CHECK(!tracks.removeTrack(master));                          // it cannot be removed or moved
    CHECK(!tracks.moveTrack(master, 0));
    CHECK(tracks.project().tracks().size() == 1 && tracks.project().tracks()[0]->id == vocal);
    CHECK(tracks.removeTrackEffect(master, limiter).accepted);
    CHECK(tracks.project().master().effects.size() == 1);
}

namespace {
std::shared_ptr<AudioData> rampAudio(std::int64_t frames)
{
    auto audio = std::make_shared<AudioData>();
    audio->sampleRate = 48000;
    audio->frames = frames;
    audio->channels.assign(1, std::vector<float>(std::size_t(frames)));
    for (std::int64_t i = 0; i < frames; ++i) audio->channels[0][std::size_t(i)] = 0.25f * float(i) / float(frames);
    audio->buildWaveform();
    return audio;
}
}

TEST(tracklist, clip_tools_reverse_flip_and_normalise_audio_clips_in_one_undo_step)
{
    using Tool = TrackList::ClipTool;
    TrackList tracks;
    tracks.setSampleRate(48000);
    const int track = tracks.addAudioTrack();
    tracks.audio()["take.wav"] = rampAudio(48000);
    const int clip = tracks.addAudioClip(track, "take.wav", 0, 12000, 6000);   // samples 6000..18000
    const int pattern = tracks.addClipAtBeat(tracks.addInstrumentTrackOfType("Harmonium"), 0, 4);
    const auto find = [&]() -> const AudioClip& { return *tracks.findClip(track, clip); };
    const auto original = find();

    CHECK(tracks.canApplyClipTool({clip}) && !tracks.canApplyClipTool({pattern}));
    CHECK(!tracks.applyClipTool({pattern}, Tool::Reverse).accepted);
    CHECK(tracks.applyClipTool({clip, pattern}, Tool::Reverse).changed);   // the pattern clip is left alone
    CHECK(find().reversed && find().sourceOffset == 48000 - 18000);
    // The reversed copy: its stretch is the same samples, backwards.
    const auto reversed = tracks.clipAudio(find());
    CHECK(reversed && reversed != tracks.audioFor("take.wav"));
    CHECK(reversed->channels[0][std::size_t(find().sourceOffset)] == tracks.audioFor("take.wav")->channels[0][17999]);
    CHECK(tracks.clipAudio(find()) == reversed);                             // made once
    CHECK(tracks.applyClipTool({clip}, Tool::Reverse).changed);              // and back again
    CHECK(find().sourceOffset == original.sourceOffset && !find().reversed);

    CHECK(tracks.applyClipTool({clip}, Tool::InvertPolarity).changed);
    CHECK(find().polarityInverted);
    CHECK(tracks.applyClipTool({clip}, Tool::Normalise).changed);
    // The loudest sample of 6000..18000 is 0.25 * 17999 / 48000: up to 0 dBFS.
    CHECK_NEAR(find().gainDb, -20.0 * std::log10(0.25 * 17999.0 / 48000.0), 1e-3);
    CHECK(!tracks.applyClipTool({clip}, Tool::Normalise).changed);         // already there
    CHECK(tracks.undo());
    CHECK(find().gainDb == 0.0f && find().polarityInverted);
    CHECK(tracks.undo() && tracks.undo() && tracks.undo());
    CHECK(find().sourceOffset == original.sourceOffset && !find().reversed && !find().polarityInverted);

    // Copy and paste keep the flags; clips with different flags do not merge.
    CHECK(tracks.applyClipTool({clip}, Tool::Reverse).changed);
    CHECK(tracks.copyClips({clip}));
    const auto pasted = tracks.pasteClips(track, 8.0);
    CHECK(pasted.size() == 1 && tracks.findClip(track, pasted[0])->reversed);
    const int right = tracks.splitClipAtBeat(track, clip, 0.125);
    CHECK(right > 0);
    CHECK(tracks.canMergeClips(track, clip, right));
    CHECK(tracks.applyClipTool({right}, Tool::InvertPolarity).changed);
    CHECK(!tracks.canMergeClips(track, clip, right));
}
