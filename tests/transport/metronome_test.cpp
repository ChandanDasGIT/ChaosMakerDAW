#include "testkit.h"
#include "engine/audioengine.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unistd.h>

namespace {
void render(AudioEngine& engine,const AudioIO& io) noexcept RT_SAFE { engine.renderBlock(io); }
void click(AudioEngine& engine,bool enabled,float volume) {
    EngineCommand command; command.type=EngineCommand::Type::SetMetronome; command.a=enabled; command.value=volume;
    CHECK(engine.post(command));
}
void configure(AudioEngine& engine,bool multi) {
    engine.deviceChanged(8000,128);
    auto snapshot=std::make_unique<EngineSnapshot>(); snapshot->multiMonitor=multi;
    snapshot->tempo.set(120,3,8,8000); engine.publishPlayback(std::move(snapshot));
}
std::vector<float> collect(AudioEngine& engine,int frames,int block) {
    std::vector<float> result;
    while(frames>0) {
        const int count=std::min(frames,block);
        std::vector<float> left(count),right(count); float* out[]{left.data(),right.data()};
        render(engine,{{nullptr,0,0},{out,2,count},nullptr,count});
        CHECK(left==right); result.insert(result.end(),left.begin(),left.end()); frames-=count;
    }
    return result;
}
double energy(const std::vector<float>& data,int start) {
    double result=0; for(int i=start;i<start+160;++i)result+=data[i]*data[i]; return result;
}
}
TEST(metronome, accented_meter_beats_are_sample_accurate_across_callbacks_and_volume_scales) {
    std::vector<float> reference;
    for(bool multi:{false,true}) for(int block:{17,257,6200}) {
        AudioEngine engine{nullptr}; configure(engine,multi); click(engine,true,1);
        CHECK(engine.post({EngineCommand::Type::Play})); auto data=collect(engine,6200,block);
        CHECK(energy(data,0)>energy(data,2000)*1.7);
        CHECK(energy(data,2000)>0.01);
        for(int i=0;i<160;++i) { CHECK_NEAR(data[i],data[6000+i],1e-6); CHECK_NEAR(data[2000+i],data[4000+i],1e-6); }
        for(int i=160;i<2000;++i) CHECK(data[i]==0);
        if(reference.empty())reference=data; else CHECK(reference==data);
        click(engine,true,0.5f); CHECK(engine.post({EngineCommand::Type::Seek,0})); auto quieter=collect(engine,200,17);
        for(int i=0;i<200;++i) CHECK_NEAR(quieter[i],reference[i]*0.5f,1e-6);
        click(engine,true,0); for(float value:collect(engine,200,17))CHECK(value==0);
        click(engine,true,1); CHECK(engine.post({EngineCommand::Type::Pause}));
        for(float value:collect(engine,200,17))CHECK(value==0);
        CHECK(engine.post({EngineCommand::Type::Play})); click(engine,false,1);
        for(float value:collect(engine,200,17))CHECK(value==0);
        engine.stop();
    }
}
TEST(metronome, loop_seek_and_tempo_changes_keep_clicks_on_the_musical_grid) {
    AudioEngine engine{nullptr}; configure(engine,true); click(engine,true,1); CHECK(engine.post({EngineCommand::Type::Play}));
    auto reference=collect(engine,6200,257);
    CHECK(engine.post({EngineCommand::Type::SetLoop,2000,6000})); CHECK(engine.post({EngineCommand::Type::Seek,0}));
    auto loop=collect(engine,9000,17);
    for(int i=0;i<9000;++i)CHECK_NEAR(loop[i],reference[i<6000 ? i : 2000+(i-6000)%4000],1e-6);
    CHECK(engine.post({EngineCommand::Type::ClearLoop})); CHECK(engine.setTempoBpm(240));
    CHECK(engine.post({EngineCommand::Type::Seek,3000})); auto changed=collect(engine,160,17);
    for(int i=0;i<160;++i)CHECK_NEAR(changed[i],reference[i],1e-6);
    engine.stop();
}
TEST(metronome, click_reaches_outputs_but_never_dry_or_wet_recordings) {
    for(bool wet:{false,true}) {
        AudioEngine engine{nullptr}; configure(engine,true); click(engine,true,1);
        const auto path="/tmp/chaosmaker-click-"+std::to_string(getpid())+(wet ? "-wet.wav" : "-dry.wav");
        TakeRecorder take(path,8000,wet ? TakeRecorder::Source::PostFx : TakeRecorder::Source::DryInput);
        RecordingGroup group; group.entries={{1,0,&take}};
        auto snapshot=std::make_unique<EngineSnapshot>(); snapshot->multiMonitor=true; snapshot->playbackEnd=0;
        snapshot->tempo.set(120,3,8,8000); TrackRender track; track.trackId=1; track.armed=true; track.inputChannel=0;
        snapshot->tracks.push_back(track); engine.publishPlayback(std::move(snapshot));
        EngineCommand command; command.type=EngineCommand::Type::StartRecordingGroup; command.group=&group; CHECK(engine.post(command));
        float input[128]{},left[128]{},right[128]{}; float* in[]{input}; float* out[]{left,right};
        render(engine,{{in,1,128},{out,2,128},nullptr,128});
        CHECK(engine.recording()); CHECK(std::any_of(left,left+128,[](float v){return std::abs(v)>0.01f;}));
        CHECK(engine.post({EngineCommand::Type::Pause})); render(engine,{{in,1,128},{out,2,128},nullptr,128}); take.wait();
        CHECK(take.audio()->frames==128); for(const auto& channel:take.audio()->channels)for(float value:channel)CHECK(value==0);
        for(float value:left)CHECK(value==0);
        engine.stop(); std::filesystem::remove(path);
    }
}
