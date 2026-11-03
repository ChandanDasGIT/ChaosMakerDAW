#include "testkit.h"
#include "engine/audioengine.h"
#include <algorithm>
#include <filesystem>
#include <unistd.h>

namespace {
void render(AudioEngine& engine,const AudioIO& io) noexcept RT_SAFE { engine.renderBlock(io); }
class StereoGain final:public Processor {
public:
    std::string typeId() const override { return "test.countInGain"; }
    void prepare(double,int) override {}
    void reset() noexcept override {}
    void process(ProcessContext& ctx) noexcept override {
        for(int c=0;c<ctx.audio.channelCount;++c)for(int i=0;i<ctx.audio.frames;++i)ctx.audio.channel(c)[i]*=c==0?2:3;
    }
};
struct Fixture {
    AudioEngine engine{nullptr};
    std::string dryPath="/tmp/chaosmaker-count-dry-"+std::to_string(getpid())+".wav";
    std::string wetPath="/tmp/chaosmaker-count-wet-"+std::to_string(getpid())+".wav";
    TakeRecorder dry{dryPath,8000},wet{wetPath,8000,TakeRecorder::Source::PostFx};
    RecordingGroup group;
    std::shared_ptr<StereoGain> gain=std::make_shared<StereoGain>();
    Fixture() {
        engine.deviceChanged(8000,128);
        group.entries={{1,0,&dry},{2,1,&wet}};
        publish(true,true);
    }
    void publish(bool first,bool second) {
        auto state=std::make_unique<EngineSnapshot>(); state->multiMonitor=true; state->tempo.set(120,3,8,8000);
        TrackRender a,b; a.trackId=1; a.inputChannel=0; a.armed=first;
        b.trackId=2; b.inputChannel=1; b.armed=second; b.monitorChain.push_back(gain.get());
        state->tracks={a,b}; state->keepProcessors.push_back(gain); engine.publishPlayback(std::move(state));
    }
    void start(std::int64_t target,int duration=6000) {
        CHECK(engine.post({EngineCommand::Type::Seek,target}));
        EngineCommand command; command.type=EngineCommand::Type::StartRecordingGroup; command.group=&group; command.a=duration;
        CHECK(engine.post(command));
    }
    std::vector<float> process(int frames,int before=-1,bool missing=false) {
        std::vector<float> a(frames,0.125f),b(frames,0.25f),left(frames),right(frames);
        if(before>0) { std::fill_n(a.begin(),std::min(before,frames),0.75f); std::fill_n(b.begin(),std::min(before,frames),0.75f); }
        float* in[]{a.data(),b.data()}; float* out[]{left.data(),right.data()};
        render(engine,{{in,missing?0:2,frames},{out,2,frames},nullptr,frames}); return left;
    }
    ~Fixture() { engine.stop(); dry.wait(); wet.wait(); std::filesystem::remove(dryPath); std::filesystem::remove(wetPath); }
};
}
TEST(countin, variable_callbacks_capture_only_after_the_exact_target_for_all_tracks) {
    for(int target:{0,8000}) {
        Fixture f; f.start(target);
        const auto click=f.process(17,17);
        CHECK(std::any_of(click.begin(),click.end(),[](float v){return std::abs(v)>0.001f;}));
        CHECK(f.engine.playing()); CHECK(!f.engine.recording()); CHECK(f.engine.countInSamplesRemaining()==5983);
        int elapsed=17;
        for(int block:{257,1024,33,2048,17,1024,256}) {
            f.process(block,block); elapsed+=block;
            CHECK(!f.engine.recording()); CHECK(f.engine.countInSamplesRemaining()==6000-elapsed);
            CHECK(f.engine.position()==std::max(0,target-6000+elapsed));
        }
        const int prefix=6000-elapsed; CHECK(prefix>0);
        const auto output=f.process(prefix+51,prefix);
        CHECK(f.engine.recording()); CHECK(f.engine.countInSamplesRemaining()==0); CHECK(f.engine.position()==target+51);
        for(int i=prefix;i<prefix+51;++i)CHECK(output[i]==0); // Count-in click stops when CLICK is off.
        CHECK(f.engine.post({EngineCommand::Type::Pause})); f.process(128);
        f.dry.wait(); f.wet.wait();
        CHECK(f.dry.startSample==target && f.wet.startSample==target);
        CHECK(f.dry.audio()->frames==51 && f.wet.audio()->frames==51);
        for(float v:f.dry.audio()->channels[0])CHECK(v==0.125f);
        for(float v:f.wet.audio()->channels[0])CHECK(v==0.5f);
        for(float v:f.wet.audio()->channels[1])CHECK(v==0.75f);
    }
}
TEST(countin, pause_stop_record_toggle_and_shutdown_cancel_without_audio) {
    for(auto type:{EngineCommand::Type::Pause,EngineCommand::Type::Stop,EngineCommand::Type::StopRecording}) {
        Fixture f; f.start(8000); f.process(257);
        CHECK(f.engine.post({type})); f.process(128); f.dry.wait(); f.wet.wait();
        CHECK(!f.engine.playing()); CHECK(!f.engine.recording()); CHECK(f.engine.countInSamplesRemaining()==0);
        CHECK(f.engine.position()==(type==EngineCommand::Type::Stop ? 0 : 8000));
        CHECK(f.dry.audio()->frames==0 && f.wet.audio()->frames==0);
    }
    Fixture f; f.start(0); f.engine.stop(); f.dry.wait(); f.wet.wait();
    CHECK(f.group.released.load()); CHECK(f.dry.audio()->frames==0 && f.wet.audio()->frames==0);
}
TEST(countin, input_loss_and_disarming_cancel_pending_capture_safely) {
    for(bool lost:{false,true}) {
        Fixture f; f.start(8000); f.process(128);
        if(!lost)f.publish(false,false);
        f.process(128,-1,lost); f.dry.wait(); f.wet.wait();
        CHECK(f.group.released.load()); CHECK(!f.engine.playing()); CHECK(!f.engine.recording());
        CHECK(f.engine.countInSamplesRemaining()==0); CHECK(f.engine.position()==8000);
        CHECK(f.dry.audio()->frames==0 && f.wet.audio()->frames==0);
    }
}
