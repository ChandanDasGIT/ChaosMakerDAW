#include "testkit.h"
#include "timeline/timeline.h"
#include "session/tracklist.h"

TEST(timeline_clipboard, queries_and_actions_manage_selection_lifecycle)
{
    TrackList tracks;
    tracks.setTempo(120.0, 4, 4);
    tracks.setSampleRate(44100.0);

    const int t1 = tracks.addAudioTrack();
    const int t2 = tracks.addAudioTrack();
    const int c1 = tracks.addClipAtBeat(t1, 0.0, 4.0);

    Timeline timeline;
    timeline.setTracks(&tracks);

    // Initially no selection
    CHECK(!timeline.canCopySelection());
    CHECK(!timeline.canCutSelection());
    CHECK(!timeline.canPaste());
    CHECK(!timeline.canDuplicateSelection());

    // Select clip c1
    timeline.selectClip(t1, c1, false, false);
    CHECK(timeline.canCopySelection());
    CHECK(timeline.canCutSelection());
    CHECK(timeline.canDuplicateSelection());
    CHECK(!timeline.canPaste()); // nothing on clipboard yet

    // Copy selected clip
    CHECK(timeline.copySelectedClips());
    CHECK(timeline.canPaste());
    CHECK(timeline.selectedClipId() == c1);

    // Paste at beat 6.0 onto t2
    const auto pasted = timeline.pasteClips(6.0, t2);
    CHECK(pasted.size() == 1);
    CHECK(timeline.selectedClipId() == pasted[0]);
    CHECK(timeline.selectedTrackId() == t2);
    CHECK(timeline.selectedIds().size() == 1);
    CHECK(timeline.selectedIds()[0] == pasted[0]);

    // Duplicate the newly pasted clip (should duplicate at beat 10.0 and select the duplicate)
    const auto duplicated = timeline.duplicateSelectedClips();
    CHECK(duplicated.size() == 1);
    CHECK(timeline.selectedClipId() == duplicated[0]);
    CHECK(timeline.selectedTrackId() == t2);
    CHECK(tracks.clips().size() == 3);

    // Cut the duplicate (should clear selection and leave 2 clips)
    const auto cut = timeline.cutSelectedClips();
    CHECK(cut.size() == 1);
    CHECK(cut[0] == duplicated[0]);
    CHECK(timeline.selectedIds().empty());
    CHECK(timeline.selectedClipId() == -1);
    CHECK(tracks.clips().size() == 2);
}

TEST(timeline_clipboard, locked_edits_and_recording_inhibit_operations)
{
    TrackList tracks;
    tracks.setTempo(120.0, 4, 4);
    tracks.setSampleRate(44100.0);

    const int t1 = tracks.addAudioTrack();
    const int c1 = tracks.addClipAtBeat(t1, 0.0, 4.0);

    Timeline timeline;
    timeline.setTracks(&tracks);
    timeline.selectClip(t1, c1, false, false);
    CHECK(timeline.copySelectedClips());

    // Inhibit via recording
    timeline.setRecording(true);
    CHECK(!timeline.canCopySelection());
    CHECK(!timeline.canCutSelection());
    CHECK(!timeline.canPaste());
    CHECK(!timeline.canDuplicateSelection());
    timeline.setRecording(false);

    // Inhibit via locked recording track
    tracks.lockRecordingTrack(t1);
    CHECK(timeline.canCopySelection()); // copy is read-only
    CHECK(!timeline.canCutSelection());
    CHECK(!timeline.canPaste());
    CHECK(!timeline.canDuplicateSelection());
    tracks.lockRecordingTrack(-1);
    CHECK(timeline.canCutSelection());
}

TEST(timeline_clipboard, with_no_clip_selected_the_time_selection_is_cut_copied_and_deleted)
{
    // restMap G15.
    TrackList tracks;
    tracks.setTempo(120.0, 4, 4);
    tracks.setSampleRate(48000.0);
    const int t1 = tracks.addAudioTrack(), t2 = tracks.addAudioTrack();
    const int c1 = tracks.addClipAtBeat(t1, 0.0, 4.0);
    const int c2 = tracks.addClipAtBeat(t2, 6.0, 2.0);
    Timeline timeline;
    timeline.setTracks(&tracks);
    CHECK(!timeline.canCopySelection() && !timeline.canDeleteSelection());
    timeline.setRangeStart(1.0); timeline.setRangeEnd(3.0);
    CHECK(timeline.rangeMode());
    CHECK(timeline.canCopySelection() && timeline.canCutSelection() && timeline.canDeleteSelection());
    // A selected clip wins over the range.
    timeline.selectClip(t1, c1, false, false);
    CHECK(!timeline.rangeMode());
    timeline.deselect();
    CHECK(timeline.copySelectedClips() && tracks.clipboard().size() == 1);
    CHECK(timeline.deleteSelection());
    CHECK(tracks.project().findTrack(t1)->audioClips.size() == 2);        // split around the range
    CHECK(tracks.findClip(t2, c2)->start == 6 * 24000);                   // gap mode: B stays
    CHECK(tracks.undo());
    timeline.setRipple(true);
    CHECK(timeline.deleteSelection());
    CHECK(tracks.findClip(t2, c2)->start == 4 * 24000);                   // ripple: 2 beats left
    CHECK(tracks.undo());
    // Recording locks the range edits like clip edits.
    timeline.setRecording(true);
    CHECK(!timeline.canDeleteSelection());
}
