#include "testkit.h"
#include "session/tracklist.h"
#include "plugins/denoise/denoise.h"

#include <cmath>
#include <cstdint>
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
    CHECK(names == (std::vector<std::string>{"Piano", "Guitar", "Strings", "Drums", "Harmonium", "Synth", "Bass Guitar"}));
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

TEST(tracklist, clip_fades_are_undoable_split_with_the_clip_and_report_the_crossfade)
{
    // restMap G8. 120 BPM at 48 kHz: a beat is 24000 samples.
    TrackList tracks;
    tracks.setSampleRate(48000);
    const int track = tracks.addAudioTrack();
    tracks.audio()["take.wav"] = rampAudio(96000);
    const int a = tracks.addAudioClip(track, "take.wav", 0, 48000, 0);
    const int b = tracks.addAudioClip(track, "take.wav", 36000, 48000, 0);   // overlaps a by half a beat
    const int pattern = tracks.addClipAtBeat(tracks.addInstrumentTrackOfType("Harmonium"), 0, 4);
    const auto info = [&](int id) {
        for (const auto& c : tracks.clips()) if (c.clipId == id) return c;
        return TrackList::ClipInfo{};
    };
    CHECK_NEAR(info(a).fadeOutBeats, 0.5, 1e-9);
    CHECK_NEAR(info(b).fadeInBeats, 0.5, 1e-9);
    CHECK_NEAR(info(a).fadeInBeats, 0.0, 1e-9);
    CHECK_NEAR(info(pattern).fadeInBeats, 0.0, 1e-9);
    CHECK(tracks.findClip(track, a)->fadeOut == 0);                         // the crossfade is not stored

    CHECK(tracks.setClipFade(track, a, false, 1.0).changed);
    CHECK(tracks.findClip(track, a)->fadeIn == 24000);
    CHECK_NEAR(info(a).fadeInBeats, 1.0, 1e-9);
    CHECK(!tracks.setClipFade(track, a, false, 1.0).changed);
    CHECK(tracks.setClipFade(track, a, true, 9.0).changed);                // kept within the clip
    CHECK(tracks.findClip(track, a)->fadeIn == 24000 && tracks.findClip(track, a)->fadeOut == 24000);
    CHECK(!tracks.setClipFade(track, pattern, false, 1.0).accepted);        // audio clips only
    CHECK(!tracks.setClipFade(track, a, false, std::numeric_limits<double>::quiet_NaN()).accepted);
    CHECK(tracks.undo());
    CHECK(tracks.findClip(track, a)->fadeOut == 0 && tracks.findClip(track, a)->fadeIn == 24000);
    CHECK(tracks.undo());
    CHECK(tracks.findClip(track, a)->fadeIn == 0);
    CHECK(tracks.redo());
    CHECK(tracks.findClip(track, a)->fadeIn == 24000);

    // A drag is one undo step.
    CHECK(tracks.setClipFade(track, b, true, 0.25).changed);
    CHECK(tracks.setClipFade(track, b, true, 0.5, true).changed);
    CHECK(tracks.findClip(track, b)->fadeOut == 12000);
    CHECK(tracks.undo());
    CHECK(tracks.findClip(track, b)->fadeOut == 0);

    // Split: the left piece keeps the fade-in, the right one the fade-out; they still merge.
    CHECK(tracks.setClipFade(track, a, true, 0.25).changed);
    const int right = tracks.splitClipAtBeat(track, a, 1.5);
    CHECK(right > 0);
    CHECK(tracks.findClip(track, a)->fadeIn == 24000 && tracks.findClip(track, a)->fadeOut == 0);
    CHECK(tracks.findClip(track, right)->fadeIn == 0 && tracks.findClip(track, right)->fadeOut == 6000);
    CHECK(tracks.canMergeClips(track, a, right));
}

TEST(tracklist, a_take_compensated_to_before_zero_is_trimmed_at_the_front)
{
    // restMap G10. A 24 kHz take in a 48 kHz project: 1000 project samples are 500 file frames.
    TrackList tracks;
    tracks.setSampleRate(48000);
    const int track = tracks.addAudioTrack();
    auto audio = std::make_shared<AudioData>(); audio->path = "late.wav"; audio->sampleRate = 24000; audio->frames = 24000;
    audio->channels = {std::vector<float>(24000, 0.2f)};
    CHECK(tracks.attachTake(track, -1000, audio, false));
    const auto& clip = tracks.project().findTrack(track)->audioClips.back();
    CHECK(clip.start == 0 && clip.sourceOffset == 500 && clip.length == 48000 - 1000);
    // A take shorter than the shift stays whole, at 0.
    auto tiny = std::make_shared<AudioData>(); tiny->path = "tiny.wav"; tiny->sampleRate = 48000; tiny->frames = 100;
    tiny->channels = {std::vector<float>(100, 0.2f)};
    CHECK(tracks.attachTake(track, -500, tiny, false));
    const auto& small = tracks.project().findTrack(track)->audioClips.back();
    CHECK(small.start == 0 && small.sourceOffset == 0 && small.length == 100);
}

namespace {
// restMap G15 fixture, 120 BPM at 48 kHz (a beat is 24000 samples). Track A (audio):
//   X 0..4 beats, Z 1.5..2.5, W 2.5..5, and a volume line from level 0.5 at 0 to 1.0 at beat 4.
// Track B (instrument): a pattern clip 0..4 beats with notes at 0.5, 1.5 and 3.5.
struct RangeProject {
    TrackList tracks;
    int a = 0, b = 0, x = 0, z = 0, w = 0, pattern = 0;
    automation::VolumeEnvelope original;
    RangeProject() {
        tracks.setSampleRate(48000);
        a = tracks.addAudioTrack();
        tracks.audio()["take.wav"] = rampAudio(480000);
        x = tracks.addAudioClip(a, "take.wav", 0, 96000, 0);
        z = tracks.addAudioClip(a, "take.wav", 36000, 24000, 0);
        w = tracks.addAudioClip(a, "take.wav", 60000, 60000, 0);
        CHECK(tracks.addEnvelopePoint(a, 0, 0.5) == 0);
        CHECK(tracks.addEnvelopePoint(a, 96000, 1.0) == 1);
        original = tracks.project().findTrack(a)->volumeEnvelope;
        b = tracks.addInstrumentTrackOfType("Harmonium");
        pattern = tracks.addClipAtBeat(b, 0, 4);
        std::vector<MidiNote> notes;
        for (double beat : {0.5, 1.5, 3.5}) notes.push_back(MidiNote{int(beat * 2), 60, beat, 0.25, 100, false});
        CHECK(tracks.setClipNotes(b, pattern, notes).changed);
    }
    const AudioClip* clipAt(int track, std::int64_t start) const {
        for (const auto& c : tracks.project().findTrack(track)->audioClips) if (c.start == start) return &c;
        return nullptr;
    }
    const automation::VolumeEnvelope& envelope() const { return tracks.project().findTrack(a)->volumeEnvelope; }
};
}

TEST(tracklist, deleting_a_time_range_splits_clips_keeps_the_line_outside_and_is_one_undo_step)
{
    RangeProject p;
    const auto historyBefore = p.tracks.canUndo();
    CHECK(historyBefore);
    CHECK(p.tracks.deleteRange(1.0, 3.0, false).changed);   // samples 24000..72000
    const auto& clips = p.tracks.project().findTrack(p.a)->audioClips;
    CHECK(clips.size() == 3);                               // X left, X right, W's end; Z is gone
    CHECK(!p.tracks.findClip(p.a, p.z));
    CHECK(p.tracks.findClip(p.a, p.x)->start == 0 && p.tracks.findClip(p.a, p.x)->length == 24000);
    const auto* xRight = p.clipAt(p.a, 72000);
    CHECK(xRight && xRight->id != p.x && xRight->id != p.w);
    CHECK(xRight->length == 24000 && xRight->sourceOffset == 72000);
    CHECK(p.tracks.findClip(p.a, p.w)->start == 72000 && p.tracks.findClip(p.a, p.w)->length == 48000);
    CHECK(p.tracks.findClip(p.a, p.w)->sourceOffset == 12000);
    // The line outside the range is unchanged; edge points hold it.
    for (std::int64_t at : {0LL, 10000LL, 24000LL, 72000LL, 80000LL, 96000LL, 200000LL})
        CHECK_NEAR(p.envelope().levelAt(at), p.original.levelAt(at), 1e-9);
    CHECK(p.envelope().points().size() == 4);
    // Pattern notes: the one inside goes; the right piece keeps its later note in time.
    CHECK(p.tracks.clipNotes(p.b, p.pattern).size() == 1);
    const auto* patternRight = p.clipAt(p.b, 72000);
    CHECK(patternRight);
    const auto rightNotes = p.tracks.clipNotes(p.b, patternRight->id);
    CHECK(rightNotes.size() == 1 && std::abs(rightNotes[0].startBeat - 0.5) < 1e-9);   // beat 3.5
    // One undo puts back both tracks and the line.
    CHECK(p.tracks.undo());
    CHECK(p.tracks.project().findTrack(p.a)->audioClips.size() == 3 && p.tracks.findClip(p.a, p.z));
    CHECK(p.tracks.findClip(p.a, p.x)->length == 96000);
    CHECK(p.envelope().sameContent(p.original));
    CHECK(p.tracks.project().findTrack(p.b)->audioClips.size() == 1 && p.tracks.clipNotes(p.b, p.pattern).size() == 3);
    CHECK(p.tracks.redo());
    CHECK(p.envelope().points().size() == 4);
    // An empty range far out changes nothing.
    CHECK(!p.tracks.deleteRange(40.0, 41.0, false).changed);
    CHECK(!p.tracks.deleteRange(2.0, 2.0, false).accepted);
}

TEST(tracklist, a_ripple_range_delete_closes_the_gap_on_every_track)
{
    RangeProject p;
    CHECK(p.tracks.deleteRange(1.0, 3.0, true).changed);
    const auto* xRight = p.clipAt(p.a, 24000);
    CHECK(xRight && xRight->id != p.x && xRight->id != p.w && xRight->sourceOffset == 72000);
    CHECK(p.tracks.findClip(p.a, p.w)->start == 24000 && p.tracks.findClip(p.a, p.w)->sourceOffset == 12000);
    // The line: unchanged before the range; after it, what was at t is now at t - 48000.
    CHECK_NEAR(p.envelope().levelAt(10000), p.original.levelAt(10000), 1e-9);
    CHECK_NEAR(p.envelope().levelAt(32000), p.original.levelAt(80000), 1e-9);
    CHECK_NEAR(p.envelope().levelAt(48000), 1.0, 1e-9);
    // The pattern's right piece moved left with its note.
    const auto* patternRight = p.clipAt(p.b, 24000);
    CHECK(patternRight && p.tracks.clipNotes(p.b, patternRight->id).size() == 1);
    CHECK(p.tracks.undo());
    CHECK(p.envelope().sameContent(p.original) && p.tracks.findClip(p.a, p.w)->start == 60000);
}

TEST(tracklist, copying_a_range_takes_clip_parts_and_the_line_and_pastes_them_together)
{
    RangeProject p;
    CHECK(p.tracks.copyRange(1.0, 3.0));
    CHECK(p.tracks.project().findTrack(p.a)->audioClips.size() == 3);   // the document stays
    CHECK(p.tracks.clipboard().size() == 4);                           // X, Z, W's start, pattern
    CHECK(p.tracks.clipboardEnvelopes().size() == 1);
    CHECK_NEAR(p.tracks.clipboardLengthBeats(), 2.0, 1e-12);
    const auto aClips = p.tracks.project().findTrack(p.a)->audioClips.size();
    const auto pasted = p.tracks.pasteClips(p.a, 8.0);                 // samples 192000..240000
    CHECK(pasted.size() == 4);
    CHECK(p.tracks.project().findTrack(p.a)->audioClips.size() == aClips + 3);
    const auto* xCopy = p.clipAt(p.a, 192000);
    CHECK(xCopy && xCopy->length == 48000 && xCopy->sourceOffset == 24000);
    const auto* wCopy = p.clipAt(p.a, 192000 + 36000);
    CHECK(wCopy && wCopy->length == 12000 && wCopy->sourceOffset == 0);
    CHECK(p.clipAt(p.b, 192000));                                      // onto the track below
    // The pasted line has the copied levels; the line before it is unchanged.
    CHECK_NEAR(p.envelope().levelAt(192000), p.original.levelAt(24000), 1e-9);
    CHECK_NEAR(p.envelope().levelAt(216000), p.original.levelAt(48000), 1e-9);
    CHECK_NEAR(p.envelope().levelAt(240000), p.original.levelAt(72000), 1e-9);
    CHECK_NEAR(p.envelope().levelAt(50000), p.original.levelAt(50000), 1e-9);
    CHECK(p.tracks.undo());                                            // one step
    CHECK(p.tracks.project().findTrack(p.a)->audioClips.size() == aClips);
    CHECK(p.envelope().sameContent(p.original));
    // Cut = copy + delete, one undo step; the clipboard keeps the cut.
    CHECK(p.tracks.cutRange(1.0, 3.0, false).changed);
    CHECK(p.tracks.clipboard().size() == 4 && !p.tracks.findClip(p.a, p.z));
    CHECK(p.tracks.undo() && p.tracks.findClip(p.a, p.z));
}

TEST(tracklist, ripple_paste_duplicate_and_clip_delete_make_and_close_room)
{
    TrackList tracks;
    tracks.setSampleRate(48000);
    const int a = tracks.addAudioTrack(), b = tracks.addAudioTrack();
    tracks.audio()["take.wav"] = rampAudio(480000);
    const int q = tracks.addAudioClip(a, "take.wav", 0, 24000, 0);        // beat 0..1
    const int later = tracks.addAudioClip(a, "take.wav", 96000, 48000, 0); // beat 4..6
    const int across = tracks.addAudioClip(b, "take.wav", 72000, 48000, 0); // beat 3..5 on B
    CHECK(tracks.addEnvelopePoint(a, 0, 0.4) == 0);
    CHECK(tracks.addEnvelopePoint(a, 120000, 0.9) == 1);                 // beat 5
    const auto levelBefore = tracks.project().findTrack(a)->volumeEnvelope.levelAt(96000);

    // Paste a 1-beat clip at beat 4 with ripple: everything from beat 4 on moves right a beat.
    CHECK(tracks.copyClips({q}));
    const auto pasted = tracks.pasteClips(a, 4.0, true);
    CHECK(pasted.size() == 1 && tracks.findClip(a, pasted[0])->start == 96000);
    CHECK(tracks.findClip(a, later)->start == 120000);
    CHECK(tracks.findClip(b, across)->start == 72000 && tracks.findClip(b, across)->length == 24000);   // split at beat 4
    const auto& bClips = tracks.project().findTrack(b)->audioClips;
    CHECK(bClips.size() == 2);
    const auto& bRight = bClips[0].id == across ? bClips[1] : bClips[0];
    CHECK(bRight.start == 120000 && bRight.length == 24000 && bRight.sourceOffset == 24000);
    const auto& line = tracks.project().findTrack(a)->volumeEnvelope;
    CHECK(line.points().back().position == 144000);                      // beat 5 -> 6
    CHECK_NEAR(line.levelAt(96000), levelBefore, 1e-9);                  // flat across the new space
    CHECK_NEAR(line.levelAt(120000), levelBefore, 1e-9);
    CHECK(tracks.undo());
    CHECK(tracks.findClip(a, later)->start == 96000 && tracks.project().findTrack(b)->audioClips.size() == 1);

    // Delete a clip with ripple: later clips and points on its track close up; B stays.
    CHECK(tracks.deleteClips({q}, true).changed);
    CHECK(!tracks.findClip(a, q) && tracks.findClip(a, later)->start == 72000);
    CHECK(tracks.project().findTrack(a)->volumeEnvelope.points().back().position == 96000);
    CHECK(tracks.findClip(b, across)->start == 72000);
    CHECK(tracks.undo() && tracks.findClip(a, q));
    // Without ripple nothing else moves.
    CHECK(tracks.deleteClips({q}, false).changed && tracks.findClip(a, later)->start == 96000);
    CHECK(tracks.undo());

    // Duplicate with ripple: the copy goes right after, later clips make room.
    const auto copies = tracks.duplicateClips({q}, 0.0, true);
    CHECK(copies.size() == 1 && tracks.findClip(a, copies[0])->start == 24000);
    CHECK(tracks.findClip(a, later)->start == 120000);
    CHECK(tracks.undo() && tracks.findClip(a, later)->start == 96000 && !tracks.findClip(a, copies[0]));
    // Cut with ripple: clipboard and the closed gap.
    CHECK(tracks.cutClips({q}, true).size() == 1 && tracks.findClip(a, later)->start == 72000);
}

TEST(tracklist, every_undo_step_has_a_name_for_the_history_list)
{
    using L = std::vector<std::string>;
    TrackList tracks;
    const int t = tracks.addAudioTrack(), u = tracks.addAudioTrack();
    const auto& project = tracks.project();
    CHECK(project.historyLabels().empty());

    const int a = tracks.addClipAtBeat(t, 0, 4);
    auto audio = std::make_shared<AudioData>(); audio->path = "take.wav"; audio->sampleRate = 48000; audio->frames = 48000;
    audio->channels = {std::vector<float>(48000, 0.2f)};
    CHECK(tracks.attachTake(t, 192000, audio, false));
    const int b = project.findTrack(t)->audioClips.back().id;
    CHECK(tracks.editClips({a}, "move", 1).changed);
    CHECK(tracks.editClips({a, b}, "move", 1).changed);
    // A drag is one step with one name.
    CHECK(tracks.setClipGain(t, a, -3.0).changed);
    CHECK(tracks.setClipGain(t, a, -6.0, true).changed);
    CHECK(tracks.setClipFade(t, b, true, 1.0).changed);
    CHECK(tracks.addEnvelopePoint(t, 0, 0.5) >= 0);
    CHECK(tracks.setEnvelopeBypassed(t, true).changed);
    CHECK(project.historyLabels() == (L{"Add clip", "Record", "Move clip", "Move clips", "Clip gain",
                                        "Fade out", "Add envelope point", "Bypass envelope"}));
    CHECK(project.historyMark() == 8);

    // An action made of others takes its own name: Cut (copy + delete) is "Cut", not "Delete".
    CHECK(tracks.cutClips({a}, false).size() == 1);
    CHECK(tracks.pasteClips(u, 0.0, false).size() == 1);
    CHECK(tracks.duplicateClips({b}, 0.0, false).size() == 1);
    CHECK(tracks.deleteRange(1.0, 2.0, true).changed);
    CHECK(tracks.removeTrack(u));
    auto labels = project.historyLabels();
    CHECK((L(labels.end() - 5, labels.end()) == L{"Cut", "Paste", "Duplicate", "Ripple delete range", "Delete track"}));

    // Undo keeps the names and moves the mark; renaming the newest step needs it to be current.
    const auto steps = project.stepCount();
    CHECK(tracks.undo() && tracks.undo());
    CHECK(project.historyLabels() == labels && project.historyMark() == labels.size() - 2);
    CHECK(!tracks.project().setLastStepLabel("Nope"));
    CHECK(tracks.redo());
    CHECK(project.historyMark() == labels.size() - 1);
    // A new step drops the undone one after it.
    CHECK(tracks.setClipGain(t, b, 2.0).changed);
    labels = project.historyLabels();
    CHECK(labels.back() == "Clip gain" && labels[labels.size() - 2] == "Ripple delete range");
    CHECK(project.stepCount() == steps + 1);

    // A refused action adds no step and renames nothing.
    CHECK(!tracks.editClips({999}, "move", 1).accepted);
    CHECK(project.historyLabels() == labels && project.stepCount() == steps + 1);

    // At the 100-step cap the size stays put but the counter still moves.
    for (int i = 0; i < 120; ++i) CHECK(tracks.setClipGain(t, b, -double(i % 7) - 1.0).accepted);
    CHECK(project.historyLabels().size() == 100 && project.stepCount() > steps + 100);
}

namespace {
// A track with one 2 s clip at 0 playing take A (48 kHz, 4 s); take B (24 kHz, 2 s) lies
// under it from the same start.
struct TakesProject {
    TrackList tracks;
    int track = -1, clip = -1;
    std::shared_ptr<AudioData> a = std::make_shared<AudioData>(), b = std::make_shared<AudioData>();
    TakesProject() {
        track = tracks.addAudioTrack();
        a->path = "takeA.wav"; a->sampleRate = 48000; a->frames = 192000; a->channels = {std::vector<float>(192000, 0.1f)};
        b->path = "takeB.wav"; b->sampleRate = 24000; b->frames = 48000; b->channels = {std::vector<float>(48000, 0.2f)};
        CHECK(tracks.attachTake(track, 0, a, false));
        CHECK(tracks.attachTake(track, 0, b, false));                 // registers B's audio
        const auto& clips = tracks.project().findTrack(track)->audioClips;
        auto withTakes = clips[0];
        withTakes.length = 96000;
        cliptakes::set(withTakes, {ClipTake{}, ClipTake{"takeB.wav", false, 0.0, {}}}, 0);
        CHECK(tracks.project().editClipGroup({{track, withTakes}, {track, clips[1], true}}));
        clip = withTakes.id;
    }
    const AudioClip& at(int id) { return *tracks.findClip(track, id); }
};
}

TEST(tracklist, a_clip_switches_between_takes_and_pieces_of_a_split_pick_their_own)
{
    TakesProject p;
    auto& t = p.tracks;
    const auto info = t.clips();
    CHECK(info.size() == 1 && info[0].takeCount == 2 && info[0].activeTake == 0);

    CHECK(t.nextTake({p.clip}).changed);
    CHECK(p.at(p.clip).poolId == "takeB.wav" && p.at(p.clip).sourceOffset == 0 && t.clips()[0].activeTake == 1);
    CHECK(t.project().historyLabels().back() == "Switch take");
    CHECK(t.undo() && p.at(p.clip).poolId == "takeA.wav");
    CHECK(t.redo() && p.at(p.clip).poolId == "takeB.wav");
    CHECK(t.nextTake({p.clip}).changed && p.at(p.clip).poolId == "takeA.wav");   // wraps around

    // Comping: split at 1 s; the right piece plays B from B's own 1 s (24000 frames at 24 kHz).
    const int right = t.splitClipAtBeat(p.track, p.clip, 2.0);
    CHECK(right > 0 && cliptakes::count(p.at(right)) == 2);
    CHECK(t.setActiveTake({right}, 1).changed);
    CHECK(p.at(right).poolId == "takeB.wav" && p.at(right).sourceOffset == 24000);
    CHECK(p.at(p.clip).poolId == "takeA.wav");
    CHECK(!t.canMergeClips(p.track, p.clip, right));                // different takes playing
    CHECK(t.setActiveTake({right}, 0).changed && p.at(right).sourceOffset == 48000);
    CHECK(t.canMergeClips(p.track, p.clip, right));
    CHECK(!t.setActiveTake({right}, 0).changed);                   // already playing

    // A moved clip keeps its takes lined up.
    CHECK(t.moveClipAtBeat(p.track, right, 8.0));
    CHECK(t.setActiveTake({right}, 1).changed && p.at(right).sourceOffset == 24000);
    CHECK(t.setActiveTake({right}, 0).changed);

    // Copy / paste carries the takes.
    CHECK(t.copyClips({right}));
    const auto pasted = t.pasteClips(p.track, 16.0, false);
    CHECK(pasted.size() == 1 && p.at(pasted[0]).takes == p.at(right).takes);

    // Reversing the playing take keeps the others where they are.
    CHECK(t.applyClipTool({p.clip}, TrackList::ClipTool::Reverse).changed);
    CHECK(p.at(p.clip).reversed && p.at(p.clip).sourceOffset == 144000);
    CHECK(t.setActiveTake({p.clip}, 1).changed && p.at(p.clip).sourceOffset == 0 && !p.at(p.clip).reversed);
    CHECK(t.setActiveTake({p.clip}, 0).changed && p.at(p.clip).reversed && p.at(p.clip).sourceOffset == 144000);
}

TEST(tracklist, takes_that_cannot_play_here_are_skipped_and_takes_can_be_deleted_or_cropped)
{
    TakesProject p;
    auto& t = p.tracks;
    // Take C starts 0.5 s after the clip: it cannot play from the clip's start.
    auto clip = p.at(p.clip);
    cliptakes::set(clip, {ClipTake{}, ClipTake{"takeA.wav", false, 24000.0, {}}, ClipTake{"takeB.wav", false, 0.0, {}}}, 0);
    CHECK(t.project().editClipGroup({{p.track, clip}}));
    CHECK(!t.setActiveTake({p.clip}, 1).changed && p.at(p.clip).sourceOffset == 0);
    CHECK(t.nextTake({p.clip}).changed && p.at(p.clip).poolId == "takeB.wav");   // skips C

    // Deleting the playing take: the next one that can play takes over (A, after wrapping).
    CHECK(t.deleteActiveTake({p.clip}).changed);
    CHECK(t.project().historyLabels().back() == "Delete take");
    CHECK(cliptakes::count(p.at(p.clip)) == 2 && p.at(p.clip).poolId == "takeA.wav");
    CHECK(t.cropToActiveTake({p.clip}).changed && p.at(p.clip).takes.list.empty());
    CHECK(t.project().historyLabels().back() == "Crop to active take");
    CHECK(t.undo() && cliptakes::count(p.at(p.clip)) == 2);
    // A clip with one take: nothing to do.
    CHECK(t.cropToActiveTake({p.clip}).changed);
    CHECK(!t.nextTake({p.clip}).changed && t.nextTake({p.clip}).accepted);
    // Refused while recording.
    CHECK(t.undo());
    t.lockRecordingTrack(p.track);
    CHECK(!t.nextTake({p.clip}).accepted);
    t.lockRecordingTrack(-1);
    // Its only other take (C) cannot play here: nothing to switch to.
    CHECK(t.nextTake({p.clip}).accepted && p.at(p.clip).poolId == "takeA.wav");
}

namespace {
std::shared_ptr<AudioData> recording(const std::string& path, std::int64_t frames, float value = 0.3f)
{
    auto audio = std::make_shared<AudioData>();
    audio->path = path; audio->sampleRate = 48000; audio->frames = frames;
    audio->channels = {std::vector<float>(std::size_t(frames), value)};
    return audio;
}
}

TEST(tracklist, loop_recording_keeps_each_complete_pass_as_a_take_of_one_clip)
{
    // A 1 s loop from 1 s (48 kHz); recorded from its start for 3.5 passes.
    TrackList tracks;
    const int t = tracks.addAudioTrack();
    TrackList::TakeShape shape;
    shape.recordStart = 48000; shape.loopStart = 48000; shape.loopEnd = 96000;
    CHECK(tracks.attachTake(t, 48000, recording("loop.wav", 168000), false, shape));
    const auto& clips = tracks.project().findTrack(t)->audioClips;
    CHECK(clips.size() == 1);
    const auto clip = clips[0];
    CHECK(clip.start == 48000 && clip.length == 48000 && clip.sourceOffset == 96000);   // the last pass plays
    CHECK(cliptakes::count(clip) == 3 && cliptakes::active(clip) == 2);
    CHECK(tracks.project().historyLabels().back() == "Record");
    CHECK(tracks.setActiveTake({clip.id}, 0).changed && tracks.findClip(t, clip.id)->sourceOffset == 0);
    CHECK(tracks.setActiveTake({clip.id}, 1).changed && tracks.findClip(t, clip.id)->sourceOffset == 48000);

    // Started half a second before the loop, with 480 samples of latency compensation: the
    // passes still line up with the loop (the file's first 24480 frames are pre-roll).
    const int u = tracks.addAudioTrack();
    shape.recordStart = 24000;
    CHECK(tracks.attachTake(u, 24000 - 480, recording("preroll.wav", 24480 + 2 * 48000 + 1000), false, shape));
    const auto pre = tracks.project().findTrack(u)->audioClips.at(0);
    CHECK(pre.start == 48000 && cliptakes::count(pre) == 2 && pre.sourceOffset == 24480 + 48000);

    // Stopped before the loop came round: the half-pass stays as recorded.
    const int v = tracks.addAudioTrack();
    shape.recordStart = 60000;
    CHECK(tracks.attachTake(v, 60000, recording("half.wav", 20000), false, shape));
    auto half = tracks.project().findTrack(v)->audioClips.at(0);
    CHECK(half.start == 60000 && half.length == 20000 && half.takes.list.empty());
    // Wrapped once but no complete pass after it: the part before the wrap is kept.
    CHECK(tracks.attachTake(v, 60000, recording("half2.wav", 60000), false, shape));
    half = tracks.project().findTrack(v)->audioClips.at(1);
    CHECK(half.start == 60000 && half.length == 36000 && half.takes.list.empty());

    // MIDI: each pass's notes, from the pass start.
    const int m = tracks.addInstrumentTrack();
    shape.recordStart = 48000;
    MidiNote first{1, 60, 0.5, 0.25, 100, false}, second{2, 62, 2.25, 0.25, 100, false};   // 24000 / 108000 in
    CHECK(tracks.attachMidiTake(m, 48000, 2 * 48000 + 10, {first, second}, shape));
    const auto pattern = tracks.project().findTrack(m)->audioClips.at(0);
    CHECK(pattern.start == 48000 && pattern.length == 48000 && cliptakes::count(pattern) == 2);
    CHECK(pattern.notes.size() == 1 && pattern.notes[0].note == 62 && std::abs(pattern.notes[0].startBeat - 0.25) < 1e-9);
    CHECK(pattern.takes.list[0].notes.size() == 1 && pattern.takes.list[0].notes[0].note == 60);
}

TEST(tracklist, punch_recording_keeps_the_range_and_the_old_part_becomes_the_first_take)
{
    TrackList tracks;
    const int t = tracks.addAudioTrack();
    CHECK(tracks.attachTake(t, 0, recording("old.wav", 192000, 0.1f), false));
    const int old = tracks.project().findTrack(t)->audioClips[0].id;
    TrackList::TakeShape shape;
    shape.recordStart = 0; shape.punchFrom = 48000; shape.punchTo = 96000;
    CHECK(tracks.attachTake(t, 0, recording("new.wav", 144000, 0.2f), false, shape));
    CHECK(tracks.project().historyLabels().back() == "Punch record");
    auto clips = tracks.project().findTrack(t)->audioClips;
    std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    CHECK(clips.size() == 3);
    CHECK(clips[0].id == old && clips[0].start == 0 && clips[0].length == 48000 && clips[0].poolId == "old.wav");
    CHECK(clips[2].start == 96000 && clips[2].length == 96000 && clips[2].sourceOffset == 96000 && clips[2].poolId == "old.wav");
    const auto punched = clips[1];
    CHECK(punched.start == 48000 && punched.length == 48000 && punched.poolId == "new.wav" && punched.sourceOffset == 48000);
    CHECK(cliptakes::count(punched) == 2 && cliptakes::active(punched) == 1);
    CHECK(tracks.setActiveTake({punched.id}, 0).changed);
    CHECK(tracks.findClip(t, punched.id)->poolId == "old.wav" && tracks.findClip(t, punched.id)->sourceOffset == 48000);
    // One undo step: the old clip whole again.
    CHECK(tracks.undo() && tracks.undo());
    CHECK(tracks.project().findTrack(t)->audioClips.size() == 1 && tracks.findClip(t, old)->length == 192000);

    // Over a gap between two clips: what was in the range is replaced.
    const int u = tracks.addAudioTrack();
    CHECK(tracks.attachTake(u, 0, recording("a.wav", 60000), false));
    CHECK(tracks.attachTake(u, 90000, recording("b.wav", 60000), false));
    CHECK(tracks.attachTake(u, 0, recording("c.wav", 192000), false, shape));
    clips = tracks.project().findTrack(u)->audioClips;
    std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    CHECK(clips.size() == 3 && clips[0].length == 48000 && clips[1].poolId == "c.wav" && clips[1].takes.list.empty());
    CHECK(clips[2].start == 96000 && clips[2].poolId == "b.wav");

    // Nothing recorded inside the range: nothing is added.
    const auto before = tracks.project().findTrack(u)->audioClips.size();
    CHECK(tracks.attachTake(u, 200000, recording("late.wav", 1000), false, shape));
    CHECK(tracks.project().findTrack(u)->audioClips.size() == before);

    // Punch and loop together: the passes, kept inside the range.
    const int v = tracks.addAudioTrack();
    shape.loopStart = 0; shape.loopEnd = 144000; shape.recordStart = 0;
    CHECK(tracks.attachTake(v, 0, recording("both.wav", 2 * 144000), false, shape));
    const auto both = tracks.project().findTrack(v)->audioClips.at(0);
    CHECK(both.start == 48000 && both.length == 48000 && cliptakes::count(both) == 2 && both.sourceOffset == 144000 + 48000);
}

TEST(tracklist, noise_reduction_learns_the_noise_from_the_time_selection)
{
    TrackList tracks;
    const int t = tracks.addAudioTrack();
    // 1 s of hiss, then 1 s of a loud tone (48 kHz; 120 BPM: 2 beats a second).
    auto audio = std::make_shared<AudioData>();
    audio->path = "vocal.wav"; audio->sampleRate = 48000; audio->frames = 96000;
    audio->channels = {std::vector<float>(96000)};
    std::uint32_t seed = 7;
    for (int i = 0; i < 96000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        audio->channels[0][std::size_t(i)] = i < 48000 ? 0.02f * (float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f)
                                                       : 0.5f * float(std::sin(0.13 * i));
    }
    CHECK(tracks.attachTake(t, 0, audio, false));
    const int clip = tracks.project().findTrack(t)->audioClips[0].id;

    CHECK(tracks.learnNoise(t, 1.0, 1.0) == "Drag a time selection over a part with only noise first.");
    CHECK(tracks.learnNoise(t, 10.0, 12.0) == "There is no recorded audio in the time selection.");
    CHECK(tracks.learnNoise(t, 0.0, 0.01).find("too short") != std::string::npos);
    CHECK(tracks.learnNoise(-5, 0.0, 1.0) == "There is no track or clip for this noise reduction.");
    CHECK(tracks.mixEffect(t, mixfx::Denoise)->profile.empty());

    // The first half second: the hiss alone, as recorded.
    CHECK(tracks.learnNoise(t, 0.0, 1.0).empty());
    const auto profile = tracks.mixEffect(t, mixfx::Denoise)->profile;
    const auto expected = Denoise::learnProfile(std::vector<float>(audio->channels[0].begin(), audio->channels[0].begin() + 24000));
    CHECK(profile.size() == expected.size());
    for (std::size_t k = 0; k < profile.size() && k < expected.size(); ++k) CHECK_NEAR(profile[k], expected[k], 1e-5);
    CHECK(tracks.ownerAudio(t, 0, 10).size() == 10 && tracks.ownerAudio(t, 200000, 200010).empty());

    // A clip's own noise reduction learns from that clip only (clip gain counts).
    CHECK(tracks.setClipGain(t, clip, -6.0).changed);
    const int fx = Project::clipEffectsId(clip);
    CHECK(tracks.learnNoise(fx, 0.0, 1.0).empty());
    const auto quieter = tracks.mixEffect(fx, mixfx::Denoise)->profile;
    CHECK(quieter.size() == profile.size());
    CHECK(std::abs(20.0 * std::log10(quieter[200] / profile[200]) + 6.0) < 0.1);
}
