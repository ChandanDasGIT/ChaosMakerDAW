#include "testkit.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "engine/audioengine.h"

TEST(qtclipediting, delete_resize_undo_redo_preserve_audio_ids_and_other_tracks) {
    TrackListWrapper tracks;
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
    TrackListWrapper tracks; const int track=tracks.addAudioTrack();
    const int clip=tracks.addAudioClip(track,"take",24000,48000,123);
    CHECK(tracks.moveClipAtBeat(track,clip,4));
    auto c=tracks.project().findTrack(track)->audioClips[0];
    CHECK(c.start==96000 && c.length==48000 && c.sourceOffset==123);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(track)->audioClips[0].start==24000);
    CHECK(tracks.redo()); CHECK(!tracks.moveClipAtBeat(track,clip,-1));
    CHECK(!tracks.moveClipAtBeat(track,clip,1e300));
}

TEST(qtclipediting, waveform_uses_clip_source_offset_and_recording_sample_rate) {
    TrackListWrapper tracks; const int track=tracks.addAudioTrack();
    auto audio=std::make_shared<AudioData>(); audio->path="wave.wav"; audio->sampleRate=24000; audio->frames=512;
    audio->channels={std::vector<float>(512)}; audio->channels[0][256]=0.8f; audio->buildWaveform();
    CHECK(tracks.attachTake(track,0,audio));
    const int clip=tracks.project().findTrack(track)->audioClips[0].id;
    auto peaks=tracks.clipWaveform(track,clip,2); CHECK(peaks.size()==2);
    CHECK(peaks[0].toList()[1].toFloat()==0); CHECK(peaks[1].toList()[1].toFloat()==0.8f);
    CHECK(tracks.clipWaveform(track,clip,1,0.5,1)[0].toList()[1].toFloat()==0.8f);
}

TEST(qtclipediting, split_and_both_trims_preserve_source_samples_and_undo_atomically) {
    TrackListWrapper tracks; const int track=tracks.addAudioTrack();
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
    TrackListWrapper tracks; const int track=tracks.addAudioTrack(),other=tracks.addAudioTrack();
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
    TrackListWrapper tracks; const int a=tracks.addAudioTrack(),b=tracks.addAudioTrack(),c=tracks.addAudioTrack();
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

TEST(qtclipediting, playback_endpoint_exists_only_when_the_arrangement_has_clips) {
    TrackListWrapper tracks;
    CHECK(tracks.playbackSnapshot()->playbackEnd==-1);
    const int track=tracks.addAudioTrack();
    CHECK(tracks.playbackSnapshot()->playbackEnd==-1);
    const int clip=tracks.addAudioClip(track,"endpoint",100,257);
    CHECK(tracks.playbackSnapshot()->playbackEnd==357);
    CHECK(tracks.removeAudioClip(track,clip)); CHECK(tracks.playbackSnapshot()->playbackEnd==-1);
    CHECK(tracks.undo()); CHECK(tracks.playbackSnapshot()->playbackEnd==357);
    CHECK(tracks.removeTrack(track)); CHECK(tracks.playbackSnapshot()->playbackEnd==-1);
}

TEST(qtclipediting, adjacent_empty_cells_merge_as_one_undoable_group) {
    TrackListWrapper tracks; const int track=tracks.addAudioTrack(),other=tracks.addAudioTrack();
    const int a=tracks.addClipAtBeat(track,0,2),b=tracks.addClipAtBeat(track,2,2),c=tracks.addClipAtBeat(track,4,2);
    const int foreign=tracks.addClipAtBeat(other,6,2);
    CHECK(tracks.canMergeClips(track,a,b)); CHECK(tracks.canMergeSelectedClips({c,a,b}));
    CHECK(!tracks.canMergeSelectedClips({a,b,foreign})); CHECK(!tracks.canMergeSelectedClips({a,a}));
    CHECK(tracks.mergeSelectedClips({c,a,b}));
    CHECK(tracks.project().findTrack(track)->audioClips.size()==1);
    CHECK(tracks.project().findTrack(track)->audioClips[0].id==a);
    CHECK(tracks.project().findTrack(track)->audioClips[0].length==144000);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(track)->audioClips.size()==3);
    CHECK(tracks.redo()); CHECK(tracks.project().findTrack(track)->audioClips.size()==1);
    CHECK(tracks.undo()); CHECK(tracks.moveClipAtBeat(track,b,2.5));
    CHECK(!tracks.canMergeSelectedClips({a,b,c})); CHECK(!tracks.mergeSelectedClips({a,b,c}));
}
TEST(qtclipediting, three_split_recorded_pieces_merge_without_changing_source_audio) {
    TrackListWrapper tracks; const int track=tracks.addAudioTrack();
    auto audio=std::make_shared<AudioData>(); audio->path="three.wav"; audio->sampleRate=24000; audio->frames=48000;
    audio->channels={std::vector<float>(48000,0.2f)}; CHECK(tracks.attachTake(track,0,audio));
    const int a=tracks.project().findTrack(track)->audioClips[0].id,b=tracks.splitClipAtBeat(track,a,2),c=tracks.splitClipAtBeat(track,b,3);
    CHECK(tracks.mergeSelectedClips({b,c,a}));
    CHECK(tracks.project().findTrack(track)->audioClips.size()==1);
    CHECK(tracks.project().findTrack(track)->audioClips[0].length==96000);
    CHECK(tracks.playbackSnapshot()->keepAudio.front().get()==audio.get());
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(track)->audioClips.size()==3);
}
TEST(qtclipediting, cross_track_move_preserves_source_offsets_and_group_history) {
    TrackListWrapper tracks; const int a=tracks.addAudioTrack(),gap=tracks.addAudioTrack(); CHECK(tracks.removeTrack(gap));
    const int b=tracks.addVocalTrack(),c=tracks.addAudioTrack();
    auto audio=std::make_shared<AudioData>(); audio->path="transfer.wav"; audio->frames=48000;
    audio->channels={std::vector<float>(48000,0.2f)}; CHECK(tracks.attachTake(a,24000,audio,false));
    const int x=tracks.project().findTrack(a)->audioClips[0].id;
    CHECK(tracks.trimClipStartAtBeat(a,x,2)); const int y=tracks.addClipAtBeat(b,4,2);
    CHECK(tracks.editClips({x},"move",2,1)); CHECK(tracks.project().findTrack(a)->audioClips.empty());
    const auto moved=tracks.project().findTrack(b)->audioClips.back();
    CHECK(moved.id==x && moved.start==96000 && moved.length==24000 && moved.sourceOffset==24000 && !moved.effectsPrinted);
    CHECK(tracks.playbackSnapshot()->keepAudio.front().get()==audio.get());
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(a)->audioClips[0].id==x);
    CHECK(tracks.redo()); CHECK(tracks.project().findTrack(a)->audioClips.empty()); CHECK(tracks.undo());
    CHECK(tracks.editClips({x,y},"move",1,1));
    CHECK(tracks.project().findTrack(a)->audioClips.empty()); CHECK(tracks.project().findTrack(b)->audioClips[0].id==x);
    CHECK(tracks.project().findTrack(c)->audioClips[0].id==y);
    CHECK(tracks.undo()); CHECK(tracks.project().findTrack(a)->audioClips[0].id==x);
    CHECK(tracks.project().findTrack(b)->audioClips[0].id==y); CHECK(tracks.project().findTrack(c)->audioClips.empty());
    CHECK(!tracks.editClips({x,y},"move",0,2)); CHECK(tracks.project().findTrack(a)->audioClips[0].id==x);
    tracks.lockRecordingTrack(b); CHECK(!tracks.editClips({x},"move",0,1));
}

TEST(qtclipediting, per_clip_gain_updates_model_role_and_is_independent_of_track_volume) {
    TrackListWrapper tracks; const int track = tracks.addAudioTrack();
    const int clip = tracks.addClipAtBeat(track, 0, 4);
    CHECK(clip > 0);

    // Initial clip gain is 0.0 dB
    CHECK_NEAR(tracks.clipGain(track, clip), 0.0, 1e-6);
    auto clipsList = tracks.clips();
    CHECK(!clipsList.isEmpty());
    CHECK_NEAR(clipsList.front().toMap().value("gainDb").toDouble(), 0.0, 1e-6);
    CHECK(!clipsList.front().toMap().value("isRecorded").toBool());

    // Track volume starts at default 1.0 (independent)
    CHECK_NEAR(tracks.project().findTrack(track)->volume, 1.0f, 1e-6);

    // Set clip gain to 3.5 dB and verify dataChanged was emitted for ClipsRole
    int dataChangedCount = 0;
    QObject::connect(&tracks, &QAbstractItemModel::dataChanged, [&](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
        if (roles.contains(TrackListWrapper::ClipsRole)) ++dataChangedCount;
    });

    // A gain change must not rebuild the clip list (that redraws the whole timeline), but the
    // engine (projectChanged) and the undo buttons (historyChanged) must still hear about it.
    int clipsChangedCount = 0, projectChangedCount = 0, historyChangedCount = 0;
    QObject::connect(&tracks, &TrackListWrapper::clipsChanged, [&] { ++clipsChangedCount; });
    QObject::connect(&tracks, &TrackListWrapper::projectChanged, [&] { ++projectChangedCount; });
    QObject::connect(&tracks, &TrackListWrapper::historyChanged, [&] { ++historyChangedCount; });

    CHECK(tracks.setClipGain(track, clip, 3.5));
    CHECK_NEAR(tracks.clipGain(track, clip), 3.5, 1e-6);
    CHECK(dataChangedCount > 0);
    CHECK(clipsChangedCount == 0);
    CHECK(projectChangedCount == 1);
    CHECK(historyChangedCount == 1);
    CHECK(tracks.canUndo());

    // Verify track model role directly
    const auto rowClips = tracks.data(tracks.index(0), TrackListWrapper::ClipsRole).toList();
    CHECK(!rowClips.isEmpty());
    CHECK_NEAR(rowClips.front().toMap().value("gainDb").toDouble(), 3.5, 1e-6);

    // Verify track volume was completely unaffected
    CHECK_NEAR(tracks.project().findTrack(track)->volume, 1.0f, 1e-6);

    // Setting track volume does not affect clip gain
    CHECK(tracks.setVolume(track, 0.5));
    CHECK_NEAR(tracks.project().findTrack(track)->volume, 0.5f, 1e-6);
    CHECK_NEAR(tracks.clipGain(track, clip), 3.5, 1e-6);

    // Verify clips role map includes the updated gainDb
    clipsList = tracks.clips();
    CHECK_NEAR(clipsList.front().toMap().value("gainDb").toDouble(), 3.5, 1e-6);

    // Undo clip gain restores previous value
    CHECK(tracks.undo());
    CHECK_NEAR(tracks.clipGain(track, clip), 0.0, 1e-6);
    CHECK_NEAR(tracks.project().findTrack(track)->volume, 0.5f, 1e-6);
}

TEST(qtclipediting, clip_selection_auto_selects_parent_track_and_notifies_changes) {
    TrackListWrapper tracks;
    const int t1 = tracks.addAudioTrack();
    const int t2 = tracks.addVocalTrack();
    const int c1 = tracks.addClipAtBeat(t1, 0, 4);
    const int c2 = tracks.addClipAtBeat(t2, 0, 4);

    CHECK(tracks.selectTrack(t1));
    CHECK(tracks.selectedTrackId() == t1);

    int trackSignalCount = 0;
    int lastNotifiedTrack = -1;
    QObject::connect(&tracks, &TrackListWrapper::selectedTrackChanged, [&](int trackId) {
        ++trackSignalCount;
        lastNotifiedTrack = trackId;
    });

    int clipSignalCount = 0;
    int lastNotifiedClip = -1;
    QObject::connect(&tracks, &TrackListWrapper::selectedClipChanged, [&](int clipId) {
        ++clipSignalCount;
        lastNotifiedClip = clipId;
    });

    // Selecting clip c2 on track t2 should auto-select track t2 and notify both signals
    tracks.setSelectedClipId(c2);
    CHECK(tracks.selectedClipId() == c2);
    CHECK(tracks.selectedTrackId() == t2);
    CHECK(clipSignalCount == 1);
    CHECK(lastNotifiedClip == c2);
    CHECK(trackSignalCount == 1);
    CHECK(lastNotifiedTrack == t2);

    // Selecting that track explicitly after clip selection must succeed and not be blocked
    CHECK(tracks.selectTrack(t2));
    CHECK(tracks.selectedTrackId() == t2);

    // Switching to track t1
    CHECK(tracks.selectTrack(t1));
    CHECK(tracks.selectedTrackId() == t1);
    CHECK(lastNotifiedTrack == t1);
    CHECK(trackSignalCount == 2);

    // Selecting clip c1 on track t1 auto-selects track t1
    tracks.selectClip(t1, c1);
    CHECK(tracks.selectedClipId() == c1);
    CHECK(tracks.selectedTrackId() == t1);
    CHECK(lastNotifiedClip == c1);

    // Switching back to track t2 works
    CHECK(tracks.selectTrack(t2));
    CHECK(tracks.selectedTrackId() == t2);
    CHECK(lastNotifiedTrack == t2);
}

TEST(qtclipediting, undoing_a_track_removal_announces_the_row) {
    TrackListWrapper tracks;
    const int a = tracks.addAudioTrack(), b = tracks.addVocalTrack(), c = tracks.addAudioTrack();
    tracks.addClipAtBeat(b, 0, 4);
    int inserted = -1, removed = -1, resets = 0;
    QObject::connect(&tracks, &QAbstractItemModel::rowsInserted, [&](const QModelIndex&, int first, int) { inserted = first; });
    QObject::connect(&tracks, &QAbstractItemModel::rowsRemoved, [&](const QModelIndex&, int first, int) { removed = first; });
    QObject::connect(&tracks, &QAbstractItemModel::modelReset, [&] { ++resets; });
    CHECK(tracks.removeTrack(b)); CHECK(removed == 1);
    CHECK(tracks.canUndo());
    CHECK(tracks.undo());
    CHECK(inserted == 1 && tracks.count() == 3 && tracks.trackIdAt(1) == b);
    CHECK(tracks.clips().size() == 1);
    removed = -1;
    CHECK(tracks.redo());
    CHECK(removed == 1 && tracks.count() == 2);
    CHECK(resets == 0);                                       // only that row changed
    CHECK(tracks.trackIdAt(0) == a && tracks.trackIdAt(1) == c);
}

TEST(qtclipediting, a_reversed_clip_plays_and_draws_backwards_and_an_inverted_one_upside_down) {
    TrackListWrapper tracks; tracks.setSampleRate(48000); const int track=tracks.addAudioTrack();
    auto audio=std::make_shared<AudioData>(); audio->path="ramp.wav"; audio->sampleRate=48000; audio->frames=4800;
    audio->channels={std::vector<float>(4800)};
    for(int i=0;i<4800;++i) audio->channels[0][std::size_t(i)]=float(i)/4800.0f;
    audio->buildWaveform();
    CHECK(tracks.attachTake(track,0,audio)); const int clip=tracks.project().findTrack(track)->audioClips[0].id;
    const auto forward=tracks.clipWaveform(track,clip,8);
    CHECK(tracks.canApplyClipTool({clip}));
    CHECK(tracks.applyClipTool({clip},"reverse"));
    CHECK(!tracks.applyClipTool({clip},"sideways"));
    CHECK(tracks.clips()[0].toMap()["reversed"].toBool());
    const auto backward=tracks.clipWaveform(track,clip,8);
    CHECK(backward.size()==8 && forward.size()==8);
    if(backward.size()==8 && forward.size()==8)
        CHECK(backward[0].toList()[1].toDouble()>backward[7].toList()[1].toDouble());   // loud end first now
    CHECK(tracks.applyClipTool({clip},"invertPolarity"));
    // Play it: the samples come out backwards and negated.
    auto snapshot=tracks.playbackSnapshot();
    CHECK(snapshot->tracks[0].clips[0].gain<0);
    AudioEngine engine{nullptr}; engine.deviceChanged(48000,256);
    engine.publishPlayback(std::move(snapshot)); CHECK(engine.post({EngineCommand::Type::Play}));
    float left[256]{}, right[256]{}; float* out[]{left,right};
    engine.renderBlock({{nullptr,0,0},{out,2,256},nullptr,256});
    CHECK(std::abs(left[0]-(-4799.0f/4800.0f))<1e-4f);
    CHECK(std::abs(left[100]-(-4699.0f/4800.0f))<1e-4f);
    engine.stop();
}

TEST(qtclipediting, clip_fades_reach_qml_with_the_crossfade_and_are_one_undo_step) {
    // restMap G8. Two takes of 2 beats overlapping by half a beat (120 BPM, 48 kHz).
    TrackListWrapper tracks; const int track=tracks.addAudioTrack();
    auto audio=std::make_shared<AudioData>(); audio->path="take.wav"; audio->sampleRate=48000; audio->frames=48000;
    audio->channels={std::vector<float>(48000,0.25f)};
    CHECK(tracks.attachTake(track,0,audio)); CHECK(tracks.attachTake(track,36000,audio));
    const auto& clips=tracks.project().findTrack(track)->audioClips;
    CHECK(clips.size()==2);
    const int a=clips[0].id, b=clips[1].id;
    const auto role=[&](int id,const char* key) {
        for(const auto& c:tracks.clips()) if(c.toMap().value("clipId").toInt()==id) return c.toMap().value(key).toDouble();
        return -1.0;
    };
    CHECK_NEAR(role(a,"fadeOutBeats"),0.5,1e-9);
    CHECK_NEAR(role(b,"fadeInBeats"),0.5,1e-9);
    int clipsChanged=0; QObject::connect(&tracks,&TrackListWrapper::clipsChanged,[&]{ ++clipsChanged; });
    CHECK(tracks.setClipFade(track,a,false,0.25));
    CHECK(clipsChanged==1);                                   // the timeline redraws the fade
    CHECK_NEAR(role(a,"fadeInBeats"),0.25,1e-9);
    CHECK(tracks.canUndo() && tracks.undo());
    CHECK_NEAR(role(a,"fadeInBeats"),0.0,1e-9);
    const int pattern=tracks.addClipAtBeat(tracks.addVocalTrack(),0,4);
    CHECK(!tracks.setClipFade(tracks.project().tracks().back()->id,pattern,false,1.0));
}
