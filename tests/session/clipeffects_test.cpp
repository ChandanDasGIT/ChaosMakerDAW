#include "testkit.h"
#include "session/tracklist.h"

// Clip effects (restMap R2): a clip's own effects, by its effects id.
namespace {
std::shared_ptr<AudioData> flatAudio(std::int64_t frames)
{
    auto audio = std::make_shared<AudioData>();
    audio->sampleRate = 48000;
    audio->frames = frames;
    audio->channels.assign(1, std::vector<float>(std::size_t(frames), 0.5f));
    audio->buildWaveform();
    return audio;
}
}

TEST(clipeffects, a_clips_effects_work_through_the_effect_calls_and_travel_with_it)
{
    TrackList tracks;
    tracks.setSampleRate(48000);
    const int track = tracks.addAudioTrack();
    tracks.audio()["take.wav"] = flatAudio(480000);
    const int clip = tracks.addAudioClip(track, "take.wav", 0, 240000, 0);   // 10 s
    const int id = Project::clipEffectsId(clip);
    CHECK(Project::isClipEffectsId(id) && !Project::isClipEffectsId(track) && !Project::isClipEffectsId(Project::MasterTrackId));
    CHECK(tracks.project().effects(id) == &tracks.findClip(track, clip)->fx);
    CHECK(tracks.project().effectOwnerType(id) == "Clip");
    int owner = -1;
    CHECK(tracks.project().clipOfEffects(id, &owner) && owner == track);
    CHECK(tracks.canHaveClipEffects(clip));

    CHECK(tracks.addTrackEffect(id, "compressor") > 0);
    CHECK(tracks.addTrackEffect(id, "guitarsuite") < 0);
    auto settings = *tracks.mixEffect(id, "compressor");
    settings.values["ratio"] = 9;
    CHECK(tracks.setMixEffect(id, "compressor", settings).changed);
    CHECK(tracks.setEqBand(id, 1, -3.0).accepted);
    CHECK(tracks.project().tracks()[0]->effects.empty());           // the track's own chain is untouched

    // Split: both pieces keep a copy. Copy / paste carries it. Undo of a later move keeps
    // the current settings.
    const int right = tracks.splitClipAtBeat(track, clip, 4.0);
    CHECK(right > 0);
    CHECK(tracks.findClip(track, right)->fx.effects.size() == 1);
    CHECK(tracks.findClip(track, right)->fx.mixEffect("compressor").values.at("ratio") == 9.0f);
    CHECK(tracks.moveClipAtBeat(track, right, 30.0));
    settings.values["ratio"] = 3;
    CHECK(tracks.setMixEffect(Project::clipEffectsId(right), "compressor", settings).changed);
    CHECK(tracks.undo());                                            // the move goes back...
    CHECK(tracks.findClip(track, right)->fx.mixEffect("compressor").values.at("ratio") == 3.0f);   // ...the setting stays
    CHECK(tracks.copyClips({clip}));
    const auto pasted = tracks.pasteClips(track, 40.0);
    CHECK(pasted.size() == 1 && tracks.findClip(track, pasted[0])->fx.effects.size() == 1);
    // A pattern clip cannot have clip effects.
    const int harmonium = tracks.addInstrumentTrackOfType("Harmonium");
    const int pattern = tracks.addClipAtBeat(harmonium, 0, 4);
    CHECK(!tracks.canHaveClipEffects(pattern));
    CHECK(tracks.addTrackEffect(Project::clipEffectsId(pattern), "compressor") < 0);
    CHECK(!tracks.project().effects(Project::clipEffectsId(99999)));
}

TEST(clipeffects, effects_on_a_selection_split_the_clip_under_it_in_one_undo_step)
{
    TrackList tracks;
    tracks.setSampleRate(48000);
    const int track = tracks.addAudioTrack();
    tracks.audio()["take.wav"] = flatAudio(960000);
    const int clip = tracks.addAudioClip(track, "take.wav", 0, 960000, 0);   // 0..20 s = beats 0..40 at 120 BPM
    const auto clipCount = [&] { return tracks.project().findTrack(track)->audioClips.size(); };

    const int middle = tracks.splitForRangeEffects(track, 20.0, 30.0);       // 10 s .. 15 s
    CHECK(middle > 0 && middle != clip);
    CHECK(clipCount() == 3);
    const auto* piece = tracks.findClip(track, middle);
    CHECK(piece && piece->start == 480000 && piece->length == 240000 && piece->sourceOffset == 480000);
    CHECK(tracks.undo());                                                   // one step: all back
    CHECK(clipCount() == 1 && tracks.findClip(track, clip)->length == 960000);

    // A range from the clip's start: one split, the left piece is the one.
    CHECK(tracks.splitForRangeEffects(track, 0.0, 10.0) == clip && clipCount() == 2);
    CHECK(tracks.undo() && clipCount() == 1);
    // A range covering the whole clip: no split.
    CHECK(tracks.splitForRangeEffects(track, 0.0, 50.0) == clip && clipCount() == 1);
    // Nothing under it, or a bad range.
    CHECK(tracks.splitForRangeEffects(track, 60.0, 70.0) == -1);
    CHECK(tracks.splitForRangeEffects(track, 30.0, 20.0) == -1);
    CHECK(tracks.splitForRangeEffects(9999, 0.0, 10.0) == -1);
}
