#include "testkit.h"
#include "engine/audioengine.h"
#include <algorithm>

namespace {
void render(AudioEngine& engine,const AudioIO& io) noexcept RT_SAFE { engine.renderBlock(io); }
std::unique_ptr<EngineSnapshot> snapshot(bool multi, std::int64_t end) {
    auto result=std::make_unique<EngineSnapshot>(); result->multiMonitor=multi; result->playbackEnd=end;
    auto audio=std::make_shared<AudioData>(); audio->frames=257;
    audio->channels={std::vector<float>(257,0.25f)};
    TrackRender track; track.clips.push_back({audio.get(),0,257});
    result->tracks.push_back(track); result->keepAudio.push_back(audio); return result;
}
}
TEST(playbackend, pauses_exactly_at_last_sample_and_silences_remaining_frames_in_both_render_paths) {
    for (bool multi:{false,true}) {
        AudioEngine engine{nullptr}; engine.deviceChanged(48000,128);
        engine.publishPlayback(snapshot(multi,257)); CHECK(engine.post({EngineCommand::Type::Play}));
        float left[200]{},right[200]{}; float* out[]{left,right};
        AudioIO io{{nullptr,0,0},{out,2,200},nullptr,200};
        render(engine,io); CHECK(engine.playing()); CHECK(engine.position()==200);
        render(engine,io); CHECK(!engine.playing()); CHECK(engine.position()==257);
        for(int i=0;i<200;++i) CHECK_NEAR(left[i],i<57 ? 0.25f : 0.0f,1e-6);
        render(engine,io); CHECK(engine.position()==257); CHECK(!engine.playing());
        for(float value:left) CHECK(value==0);
        CHECK(engine.post({EngineCommand::Type::Seek,999})); CHECK(engine.post({EngineCommand::Type::Play}));
        render(engine,io); CHECK(engine.position()==257); CHECK(!engine.playing());
        engine.stop();
    }
}
TEST(playbackend, metronome_keeps_playing_past_the_end_until_it_is_turned_off) {
    for (bool multi:{false,true}) {
        AudioEngine engine{nullptr}; engine.deviceChanged(48000,128);
        engine.publishPlayback(snapshot(multi,257));
        EngineCommand metronome; metronome.type=EngineCommand::Type::SetMetronome; metronome.a=1; metronome.value=0.5f;
        CHECK(engine.post(metronome)); CHECK(engine.post({EngineCommand::Type::Play}));
        float left[200]{},right[200]{}; float* out[]{left,right};
        AudioIO io{{nullptr,0,0},{out,2,200},nullptr,200};
        for(int i=1;i<=3;++i) { render(engine,io); CHECK(engine.playing()); CHECK(engine.position()==200*i); }
        // Metronome off: the usual stop at the end applies again.
        metronome.a=0; CHECK(engine.post(metronome)); render(engine,io);
        CHECK(!engine.playing()); CHECK(engine.position()==257);
        engine.stop();
    }
}
TEST(playbackend, repeat_can_extend_past_content_and_empty_projects_keep_playing) {
    AudioEngine engine{nullptr}; engine.deviceChanged(48000,128);
    engine.publishPlayback(snapshot(true,257));
    CHECK(engine.post({EngineCommand::Type::SetLoop,128,512})); CHECK(engine.post({EngineCommand::Type::Play}));
    float left[128]{},right[128]{}; float* out[]{left,right}; AudioIO io{{nullptr,0,0},{out,2,128},nullptr,128};
    for(int i=0;i<20;++i) { render(engine,io); CHECK(engine.playing()); CHECK(engine.position()>=128 && engine.position()<=512); }
    CHECK(engine.post({EngineCommand::Type::ClearLoop})); render(engine,io);
    CHECK(!engine.playing()); CHECK(engine.position()==257);
    auto empty=std::make_unique<EngineSnapshot>(); empty->multiMonitor=true;
    engine.publishPlayback(std::move(empty)); CHECK(engine.post({EngineCommand::Type::Play})); render(engine,io);
    CHECK(engine.playing()); CHECK(engine.position()==385);
    render(engine,io); CHECK(engine.playing()); CHECK(engine.position()==513); engine.stop();
}

TEST(playbackend, repeat_plays_lead_in_then_wraps_from_thirty_seconds_to_twenty) {
    for(bool multi:{false,true}) {
        AudioEngine engine{nullptr}; engine.deviceChanged(48000,128);
        auto state=std::make_unique<EngineSnapshot>(); state->multiMonitor=multi;
        engine.publishPlayback(std::move(state));
        constexpr std::int64_t start=20*48000,end=30*48000;
        CHECK(engine.post({EngineCommand::Type::SetLoop,start,end})); CHECK(engine.post({EngineCommand::Type::Play}));
        float left[128]{},right[128]{}; float* out[]{left,right}; AudioIO io{{nullptr,0,0},{out,2,128},nullptr,128};
        render(engine,io); CHECK(engine.position()==128); CHECK(engine.playing());
        CHECK(engine.post({EngineCommand::Type::Seek,start+24000})); render(engine,io);
        CHECK(engine.position()==start+24128);
        CHECK(engine.post({EngineCommand::Type::Seek,end-64})); render(engine,io);
        CHECK(engine.position()==start+64);
        CHECK(engine.post({EngineCommand::Type::Pause})); render(engine,io);
        CHECK(engine.position()==start+64); CHECK(!engine.playing());
        CHECK(engine.post({EngineCommand::Type::Play})); render(engine,io); CHECK(engine.position()==start+192);
        engine.stop();
    }
}
