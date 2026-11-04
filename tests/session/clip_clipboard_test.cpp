#include "testkit.h"
#include "session/tracklist.h"

#include <cmath>

TEST(clip_clipboard, copy_and_paste_single_clip_preserves_length_and_supports_undo_redo)
{
    TrackList tracks;
    tracks.setTempo(120.0, 4, 4);
    tracks.setSampleRate(44100.0);

    const int t1 = tracks.addAudioTrack();
    const int t2 = tracks.addAudioTrack();
    const int c1 = tracks.addClipAtBeat(t1, 0.0, 4.0);
    CHECK(c1 >= 0);
    CHECK(tracks.clips().size() == 1);

    // Copy clip
    CHECK(tracks.copyClips({c1}));
    CHECK(tracks.hasClipboard());
    CHECK(tracks.clipboardClipCount() == 1);

    // Paste onto t2 at beat 8.0
    const auto pasted = tracks.pasteClips(t2, 8.0);
    CHECK(pasted.size() == 1);
    CHECK(tracks.clips().size() == 2);

    const auto* pastedClip = tracks.findClip(t2, pasted[0]);
    CHECK(pastedClip != nullptr);
    if (pastedClip) {
        const double pastedBeat = tracks.project().tempo().beatAt(pastedClip->start);
        CHECK(std::abs(pastedBeat - 8.0) < 1e-4);
        const double pastedLen = tracks.project().tempo().beatAt(pastedClip->start + pastedClip->length) - pastedBeat;
        CHECK(std::abs(pastedLen - 4.0) < 1e-4);
    }

    // Single undo removes pasted clip
    CHECK(tracks.canUndo());
    CHECK(tracks.undo());
    CHECK(tracks.clips().size() == 1);
    CHECK(tracks.findClip(t2, pasted[0]) == nullptr);

    // Redo restores it
    CHECK(tracks.canRedo());
    CHECK(tracks.redo());
    CHECK(tracks.clips().size() == 2);
    CHECK(tracks.findClip(t2, pasted[0]) != nullptr);
}

TEST(clip_clipboard, copy_and_paste_multiple_clips_preserves_relative_beats_and_track_offsets)
{
    TrackList tracks;
    tracks.setTempo(120.0, 4, 4);
    tracks.setSampleRate(44100.0);

    const int t1 = tracks.addAudioTrack();
    const int t2 = tracks.addAudioTrack();
    const int t3 = tracks.addAudioTrack();

    // Clip 1 on track 1 at beat 2.0 (len 2)
    const int c1 = tracks.addClipAtBeat(t1, 2.0, 2.0);
    // Clip 2 on track 2 at beat 4.0 (len 3)
    const int c2 = tracks.addClipAtBeat(t2, 4.0, 3.0);
    CHECK(c1 >= 0 && c2 >= 0);

    CHECK(tracks.copyClips({c1, c2}));
    CHECK(tracks.clipboardClipCount() == 2);

    // Paste onto track 2 at beat 10.0
    // Relative to earliest (c1 at beat 2, track row 0):
    // c1 was beat offset 0, track offset 0 -> will go to track row 1 (t2) at beat 10.0
    // c2 was beat offset 2, track offset 1 -> will go to track row 2 (t3) at beat 12.0
    const auto pasted = tracks.pasteClips(t2, 10.0);
    CHECK(pasted.size() == 2);
    CHECK(tracks.clips().size() == 4);

    const auto* p1 = tracks.findClip(t2, pasted[0]);
    const auto* p2 = tracks.findClip(t3, pasted[1]);
    CHECK(p1 != nullptr && p2 != nullptr);
    if (p1 && p2) {
        CHECK(std::abs(tracks.project().tempo().beatAt(p1->start) - 10.0) < 1e-4);
        CHECK(std::abs(tracks.project().tempo().beatAt(p2->start) - 12.0) < 1e-4);
    }

    // Single undo removes both pasted clips atomically
    CHECK(tracks.undo());
    CHECK(tracks.clips().size() == 2);
    CHECK(tracks.findClip(t2, pasted[0]) == nullptr);
    CHECK(tracks.findClip(t3, pasted[1]) == nullptr);
}

TEST(clip_clipboard, cut_and_duplicate_behave_atomically)
{
    TrackList tracks;
    tracks.setTempo(120.0, 4, 4);
    tracks.setSampleRate(44100.0);

    const int t1 = tracks.addAudioTrack();
    const int c1 = tracks.addClipAtBeat(t1, 1.0, 2.0);
    CHECK(tracks.clips().size() == 1);

    // Duplicate places adjacent duplicate at beat 3.0
    const auto dup1 = tracks.duplicateClips({c1});
    CHECK(dup1.size() == 1);
    CHECK(tracks.clips().size() == 2);
    const auto* d1 = tracks.findClip(t1, dup1[0]);
    CHECK(d1 != nullptr);
    if (d1) {
        CHECK(std::abs(tracks.project().tempo().beatAt(d1->start) - 3.0) < 1e-4);
    }

    // Cut duplicate
    const auto cutIds = tracks.cutClips({dup1[0]});
    CHECK(cutIds.size() == 1);
    CHECK(tracks.clips().size() == 1);
    CHECK(tracks.hasClipboard());

    // Undo cut restores the clip
    CHECK(tracks.undo());
    CHECK(tracks.clips().size() == 2);
    CHECK(tracks.findClip(t1, dup1[0]) != nullptr);
}
