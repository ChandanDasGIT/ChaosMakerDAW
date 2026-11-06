#include "testkit.h"
#include "session/tracklist.h"

#include <algorithm>

namespace {
MidiNote note(int pitch, double start, double length = 0.5, int velocity = 100)
{
    MidiNote n;
    n.note = pitch; n.startBeat = start; n.lengthBeats = length; n.velocity = velocity;
    return n;
}
std::vector<double> starts(const std::vector<MidiNote>& notes)
{
    std::vector<double> result;
    for (const auto& n : notes) result.push_back(n.startBeat);
    std::sort(result.begin(), result.end());
    return result;
}
bool near(const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > 1e-6) return false;
    return true;
}
int clipAt(const TrackList& tracks, double startBeat)
{
    for (const auto& c : tracks.clips()) if (std::abs(c.startBeat - startBeat) < 1e-6) return c.clipId;
    return -1;
}
}

TEST(clipnotes, set_get_undo_and_selection_is_not_document_state)
{
    TrackList tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 2, 4);
    CHECK(tracks.clipNotes(t, c).empty());

    auto n = note(60, 1.0);
    n.selected = true;
    const auto change = tracks.setClipNotes(t, c, {n, note(64, 2.0)});
    CHECK(change.accepted && change.changed);
    const auto stored = tracks.clipNotes(t, c);
    CHECK(stored.size() == 2);
    CHECK(near(starts(stored), {1.0, 2.0}));
    CHECK(std::none_of(stored.begin(), stored.end(), [](const auto& x) { return x.selected; }));

    // ClipInfo carries the same clip-relative notes for the timeline
    const auto info = tracks.clips();
    CHECK(info.size() == 1 && info[0].notes.size() == 2);

    // Same notes again: accepted, unchanged, and no extra history step
    const auto same = tracks.setClipNotes(t, c, stored);
    CHECK(same.accepted && !same.changed);
    CHECK(tracks.undo());
    CHECK(tracks.clipNotes(t, c).empty());
    CHECK(tracks.redo());
    CHECK(tracks.clipNotes(t, c).size() == 2);
}

TEST(clipnotes, rejects_recorded_clips_invalid_notes_and_locked_edits)
{
    TrackList tracks;
    const int t = tracks.addAudioTrack();
    const int pattern = tracks.addClipAtBeat(t, 0, 4);
    const int recorded = tracks.addAudioClip(t, "/media/take.wav", 0, 48000);
    CHECK(!tracks.setClipNotes(t, recorded, {note(60, 0)}).accepted);
    CHECK(!tracks.setClipNotes(t, pattern, {note(128, 0)}).accepted);
    CHECK(!tracks.setClipNotes(t, pattern, {note(60, 0, 0.0)}).accepted);
    CHECK(!tracks.setClipNotes(t, 9999, {note(60, 0)}).accepted);
    tracks.lockRecordingTrack(t);
    CHECK(!tracks.setClipNotes(t, pattern, {note(60, 0)}).accepted);
    tracks.lockRecordingTrack(-1);
    CHECK(tracks.setClipNotes(t, pattern, {note(60, 0)}).accepted);
}

TEST(clipnotes, move_keeps_notes_and_left_trim_keeps_them_fixed_in_time)
{
    TrackList tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    CHECK(tracks.setClipNotes(t, c, {note(60, 0.5), note(62, 2.0)}).accepted);

    CHECK(tracks.moveClipAtBeat(t, c, 4.0));
    CHECK(near(starts(tracks.clipNotes(t, c)), {0.5, 2.0}));

    // Trim 1 beat off the left: notes stay put in time, so clip-relative starts drop by 1.
    // The note that falls before the new start is kept (hidden), not deleted.
    CHECK(tracks.editClips({c}, "trimStart", 1.0));
    CHECK(near(starts(tracks.clipNotes(t, c)), {-0.5, 1.0}));
    CHECK(tracks.trimClipStartAtBeat(t, c, 4.0)); // extend back to the original start
    CHECK(near(starts(tracks.clipNotes(t, c)), {0.5, 2.0}));
}

TEST(clipnotes, clip_start_beat_follows_moves_trims_and_tempo)
{
    TrackList tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 2, 4);
    CHECK(std::abs(tracks.clipStartBeat(t, c) - 2.0) < 1e-6);

    CHECK(tracks.moveClipAtBeat(t, c, 6.0));
    CHECK(std::abs(tracks.clipStartBeat(t, c) - 6.0) < 1e-6);

    CHECK(tracks.editClips({c}, "trimStart", 1.0));
    CHECK(std::abs(tracks.clipStartBeat(t, c) - 7.0) < 1e-6);

    // The clip stays at the same time, so doubling the tempo doubles its start beat.
    const double bpm = tracks.project().tempo().bpm();
    CHECK(tracks.setTempo(bpm * 2, 4, 4));
    CHECK(std::abs(tracks.clipStartBeat(t, c) - 14.0) < 1e-6);

    CHECK(tracks.clipStartBeat(t, 9999) == 0.0);
}

TEST(clipnotes, split_partitions_notes_and_merge_restores_them)
{
    TrackList tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    CHECK(tracks.setClipNotes(t, c, {note(60, 0.5), note(62, 1.5, 1.0), note(64, 2.5)}).accepted);

    const int right = tracks.splitClipAtBeat(t, c, 2.0);
    CHECK(right > 0);
    CHECK(near(starts(tracks.clipNotes(t, c)), {0.5, 1.5}));   // crossing note stays left
    CHECK(near(starts(tracks.clipNotes(t, right)), {0.5}));    // 2.5 -> 0.5 from the right half's start

    // One undo step restores the single clip with every note
    CHECK(tracks.undo());
    CHECK(tracks.clips().size() == 1);
    CHECK(near(starts(tracks.clipNotes(t, c)), {0.5, 1.5, 2.5}));
    CHECK(tracks.redo());

    CHECK(tracks.mergeClips(t, c, right));
    const int merged = clipAt(tracks, 0.0);
    CHECK(tracks.clips().size() == 1);
    CHECK(near(starts(tracks.clipNotes(t, merged)), {0.5, 1.5, 2.5}));
}

TEST(clipnotes, copy_paste_and_duplicate_carry_notes)
{
    TrackList tracks;
    const int t = tracks.addInstrumentTrack();
    const int c = tracks.addClipAtBeat(t, 0, 4);
    CHECK(tracks.setClipNotes(t, c, {note(60, 0.5), note(67, 3.0)}).accepted);

    CHECK(tracks.copyClips({c}));
    const auto pasted = tracks.pasteClips(t, 8.0);
    CHECK(pasted.size() == 1);
    CHECK(near(starts(tracks.clipNotes(t, pasted[0])), {0.5, 3.0}));

    const auto dup = tracks.duplicateClips({c});
    CHECK(dup.size() == 1);
    CHECK(near(starts(tracks.clipNotes(t, dup[0])), {0.5, 3.0}));

    // Copies are independent
    CHECK(tracks.setClipNotes(t, dup[0], {}).accepted);
    CHECK(tracks.clipNotes(t, c).size() == 2);
}
