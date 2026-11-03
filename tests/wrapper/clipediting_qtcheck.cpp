#include "testkit.h"
#include "wrapper/session/tracklistmodel.h"

TEST(qtclipediting, delete_resize_undo_redo_preserve_audio_ids_and_other_tracks) {
    TrackListModel tracks;
    const int a=tracks.addAudioTrack(), b=tracks.addVocalTrack();
    auto audio=std::make_shared<AudioData>(); audio->path="take.wav"; audio->frames=48000;
    audio->channels={std::vector<float>(48000,0.25f)};
    CHECK(tracks.attachTake(a,24000,audio)); const int clip=tracks.project().findTrack(a)->audioClips[0].id;
    const int other=tracks.addClipAtBeat(b,0,4);
    CHECK(tracks.resizeClipAtBeat(a,clip,1)); CHECK(tracks.removeAudioClip(a,clip));
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(a)->audioClips[0].id==clip);
    CHECK(tracks.project().findTrack(a)->audioClips[0].length==24000);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(a)->audioClips[0].length==48000);
    CHECK(tracks.playbackSnapshot()->tracks[0].clips[0].audio==audio.get());
    CHECK(tracks.project().findTrack(b)->audioClips[0].id==other);
    CHECK(tracks.redo()); CHECK(tracks.redo()); CHECK(tracks.project().findTrack(a)->audioClips.empty());
    CHECK(tracks.undo()); CHECK(tracks.resizeClipAtBeat(a,clip,1.5)); CHECK(!tracks.canRedo());
    tracks.lockRecordingTrack(a); CHECK(!tracks.undo()); CHECK(!tracks.canUndo());
    tracks.lockRecordingTrack(-1); CHECK(tracks.undo());
}

TEST(qtclipediting, move_is_one_undoable_edit_and_keeps_source_audio_unchanged) {
    TrackListModel tracks; const int track=tracks.addAudioTrack();
    const int clip=tracks.addAudioClip(track,"take",24000,48000,123);
    CHECK(tracks.moveClipAtBeat(track,clip,4));
    auto c=tracks.project().findTrack(track)->audioClips[0];
    CHECK(c.start==96000 && c.length==48000 && c.sourceOffset==123);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(track)->audioClips[0].start==24000);
    CHECK(tracks.redo()); CHECK(!tracks.moveClipAtBeat(track,clip,-1));
    CHECK(!tracks.moveClipAtBeat(track,clip,1e300));
}

TEST(qtclipediting, waveform_uses_clip_source_offset_and_recording_sample_rate) {
    TrackListModel tracks; const int track=tracks.addAudioTrack();
    auto audio=std::make_shared<AudioData>(); audio->path="wave.wav"; audio->sampleRate=24000; audio->frames=512;
    audio->channels={std::vector<float>(512)}; audio->channels[0][256]=0.8f; audio->buildWaveform();
    CHECK(tracks.attachTake(track,0,audio));
    const int clip=tracks.project().findTrack(track)->audioClips[0].id;
    auto peaks=tracks.clipWaveform(track,clip,2); CHECK(peaks.size()==2);
    CHECK(peaks[0].toList()[1].toFloat()==0); CHECK(peaks[1].toList()[1].toFloat()==0.8f);
    CHECK(tracks.clipWaveform(track,clip,1,0.5,1)[0].toList()[1].toFloat()==0.8f);
}

TEST(qtclipediting, split_and_both_trims_preserve_source_samples_and_undo_atomically) {
    TrackListModel tracks; const int track=tracks.addAudioTrack();
    auto audio=std::make_shared<AudioData>(); audio->path="edit.wav"; audio->sampleRate=24000; audio->frames=48000;
    audio->channels={std::vector<float>(48000,0.2f)}; CHECK(tracks.attachTake(track,0,audio));
    const int id=tracks.project().findTrack(track)->audioClips[0].id;
    const int right=tracks.splitClipAtBeat(track,id,2); CHECK(right>id);
    auto clips=tracks.project().findTrack(track)->audioClips;
    CHECK(clips[0].length==48000); CHECK(clips[1].start==48000 && clips[1].sourceOffset==24000);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(track)->audioClips.size()==1);
    CHECK(tracks.redo()); CHECK(tracks.trimClipStartAtBeat(track,right,3));
    auto c=tracks.project().findTrack(track)->audioClips[1]; CHECK(c.start==72000 && c.length==24000 && c.sourceOffset==36000);
    CHECK(tracks.trimClipStartAtBeat(track,right,2)); CHECK(tracks.resizeClipAtBeat(track,right,1));
    CHECK(!tracks.resizeClipAtBeat(track,right,3)); CHECK(tracks.undo());
    CHECK(tracks.project().findTrack(track)->audioClips[1].length==48000);
    CHECK(tracks.splitClipAtBeat(track,id,0)==-1); CHECK(!tracks.trimClipStartAtBeat(track,right,4));
    CHECK(audio->frames==48000 && audio->channels[0].size()==48000);
}

TEST(qtclipediting, merging_split_clips_preserves_audio_and_is_one_undoable_edit) {
    TrackListModel tracks; const int track=tracks.addAudioTrack(),other=tracks.addAudioTrack();
    auto audio=std::make_shared<AudioData>(); audio->path="merge.wav"; audio->sampleRate=24000; audio->frames=48000;
    audio->channels={std::vector<float>(48000,0.25f)}; CHECK(tracks.attachTake(track,0,audio));
    const int left=tracks.project().findTrack(track)->audioClips[0].id;
    const int right=tracks.splitClipAtBeat(track,left,2); CHECK(right>=0);
    const int unrelated=tracks.addClipAtBeat(other,0,4);
    CHECK(!tracks.canMergeClips(track,left,unrelated)); CHECK(!tracks.mergeClips(track,left,left));
    CHECK(tracks.canMergeClips(track,right,left)); CHECK(tracks.mergeClips(track,right,left));
    auto clips=tracks.project().findTrack(track)->audioClips;
    CHECK(clips.size()==1); CHECK(clips[0].id==left && clips[0].start==0 && clips[0].length==96000 && clips[0].sourceOffset==0);
    CHECK(tracks.playbackSnapshot()->keepAudio.front().get()==audio.get());
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(track)->audioClips.size()==2);
    CHECK(tracks.redo()); CHECK(tracks.project().findTrack(track)->audioClips.size()==1);
    CHECK(tracks.undo()); CHECK(tracks.moveClipAtBeat(track,right,3));
    CHECK(!tracks.canMergeClips(track,left,right)); CHECK(!tracks.mergeClips(track,left,right));
    CHECK(tracks.undo()); tracks.lockRecordingTrack(track);
    CHECK(!tracks.mergeClips(track,left,right)); tracks.lockRecordingTrack(-1);
    CHECK(tracks.project().findTrack(other)->audioClips.size()==1);
    const int different=tracks.addAudioClip(track,"different.wav",48000,48000,24000);
    CHECK(!tracks.canMergeClips(track,left,different));
}

TEST(qtclipediting, group_edits_across_tracks_are_atomic_and_preserve_offsets) {
    TrackListModel tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack(),c=tracks.addAudioTrack();
    const int x=tracks.addAudioClip(a,"x",48000,96000,24000);
    const int y=tracks.addAudioClip(b,"y",72000,144000,48000);
    const int z=tracks.addClipAtBeat(c,2,4);
    QVariantList ids{x,y};
    CHECK(tracks.editClips(ids,"move",2));
    CHECK(tracks.project().findTrack(a)->audioClips[0].start==96000);
    CHECK(tracks.project().findTrack(b)->audioClips[0].start==120000);
    CHECK(tracks.project().findTrack(b)->audioClips[0].sourceOffset==48000);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(b)->audioClips[0].start==72000);
    CHECK(tracks.redo()); CHECK(tracks.undo());
    CHECK(!tracks.editClips(ids,"move",-3));
    CHECK(tracks.project().findTrack(b)->audioClips[0].start==72000);
    CHECK(tracks.editClips(ids,"trimStart",1));
    CHECK(tracks.project().findTrack(a)->audioClips[0].sourceOffset==48000);
    CHECK(tracks.project().findTrack(b)->audioClips[0].sourceOffset==72000);
    CHECK(tracks.undo()); CHECK(tracks.editClips(ids,"resize",-1));
    CHECK(tracks.project().findTrack(a)->audioClips[0].length==72000);
    CHECK(tracks.project().findTrack(b)->audioClips[0].length==120000);
    CHECK(tracks.undo()); CHECK(!tracks.editClips({x,99999},"delete"));
    CHECK(tracks.project().findTrack(a)->audioClips.size()==1);
    CHECK(tracks.editClips(ids,"delete"));
    CHECK(tracks.project().findTrack(a)->audioClips.empty()); CHECK(tracks.project().findTrack(b)->audioClips.empty());
    CHECK(tracks.project().findTrack(c)->audioClips[0].id==z);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(a)->audioClips[0].id==x);
    CHECK(tracks.project().findTrack(b)->audioClips[0].id==y);
    tracks.lockRecordingTrack(a); CHECK(!tracks.editClips(ids,"move",1));
}
