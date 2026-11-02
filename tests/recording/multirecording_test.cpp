#include "testkit.h"
#include "engine/audioengine.h"
#include <filesystem>
#include <unistd.h>

namespace {
void process(AudioEngine& engine,const AudioIO& io) noexcept RT_SAFE { engine.renderBlock(io); }
class StereoGain final : public Processor {
public:
    std::string typeId() const override { return "test.multiStereoGain"; }
    void prepare(double,int) override {}
    void reset() noexcept override {}
    void process(ProcessContext& ctx) noexcept override {
        for(int c=0;c<ctx.audio.channelCount;++c)for(int i=0;i<ctx.audio.frames;++i)
            ctx.audio.channel(c)[i]*=c==0?2.0f:3.0f;
    }
};
struct Fixture {
    AudioEngine engine{nullptr};
    std::string a="/tmp/chaosmaker-multi-a-"+std::to_string(getpid())+".wav";
    std::string b="/tmp/chaosmaker-multi-b-"+std::to_string(getpid())+".wav";
    TakeRecorder wet{a,48000,TakeRecorder::Source::PostFx}, dry{b,48000};
    RecordingGroup group;
    std::shared_ptr<StereoGain> gain=std::make_shared<StereoGain>();
    std::shared_ptr<TrackMeters> meterA=std::make_shared<TrackMeters>(),meterB=std::make_shared<TrackMeters>();
    Fixture() {
        engine.deviceChanged(48000,128);
        group.entries={{1,0,&wet},{2,1,&dry}};
        CHECK(engine.post({EngineCommand::Type::SetCleanMonitor,1}));
        publish(true);
    }
    void publish(bool armedA) {
        auto snapshot=std::make_unique<EngineSnapshot>(); snapshot->multiMonitor=true;
        TrackRender a,b; a.trackId=1; a.inputChannel=0; a.armed=armedA; a.monitoring=armedA; a.muted=true;
        a.monitorChain.push_back(gain.get()); a.meters=meterA;
        b.trackId=2; b.inputChannel=1; b.armed=true; b.monitoring=true; b.gain=0.5f; b.meters=meterB;
        snapshot->tracks={a,b}; snapshot->keepProcessors.push_back(gain); engine.publishPlayback(std::move(snapshot));
    }
    void start() { EngineCommand command; command.type=EngineCommand::Type::StartRecordingGroup; command.group=&group; CHECK(engine.post(command)); }
    ~Fixture() { engine.stop(); wet.wait(); dry.wait(); std::filesystem::remove(a); std::filesystem::remove(b); }
};
}
TEST(multirecording, wet_and_dry_inputs_start_together_with_variable_blocks_and_independent_meters) {
    Fixture f; CHECK(f.engine.post({EngineCommand::Type::Seek,480})); f.start();
    int frames=0;
    for(int count:{17,257,64,1024,3}) {
        std::vector<float> a(count,0.25f),b(count,0.4f),left(count),right(count);
        float* in[]{a.data(),b.data()}; float* out[]{left.data(),right.data()};
        process(f.engine,{{in,2,count},{out,2,count},nullptr,count}); frames+=count;
        for(float value:left)CHECK_NEAR(value,0.2,1e-6); // muted wet track never reaches the speakers
    }
    CHECK(f.engine.recording()); CHECK(f.engine.position()==480+frames);
    CHECK(f.engine.post({EngineCommand::Type::Pause}));
    float left[1]{},right[1]{}; float* out[]{left,right};
    process(f.engine,{{nullptr,0,0},{out,2,1},nullptr,1});
    CHECK(f.group.released.load()); f.wet.wait(); f.dry.wait();
    CHECK(f.wet.startSample==480 && f.dry.startSample==480);
    CHECK(f.wet.audio()->frames==frames && f.dry.audio()->frames==frames);
    CHECK(f.wet.audio()->channels.size()==2); CHECK(f.dry.audio()->channels.size()==1);
    for(int i=0;i<frames;++i) {
        CHECK_NEAR(f.wet.audio()->channels[0][i],0.5,1e-6);
        CHECK_NEAR(f.wet.audio()->channels[1][i],0.75,1e-6);
        CHECK_NEAR(f.dry.audio()->channels[0][i],0.4,1e-6);
    }
    CHECK_NEAR(f.meterA->input[0].takePeak(),0.25,1e-6); CHECK(f.meterA->output[0].takePeak()==0);
    CHECK_NEAR(f.meterB->output[1].takePeak(),0.2,1e-6);
}
TEST(multirecording, disarm_releases_only_one_writer_and_stop_releases_the_other) {
    Fixture f; f.start();
    float a[128]{},b[128]{},left[128]{},right[128]{};
    float* in[]{a,b}; float* out[]{left,right}; AudioIO io{{in,2,128},{out,2,128},nullptr,128};
    process(f.engine,io); f.publish(false); process(f.engine,io);
    CHECK(f.wet.released()); CHECK(!f.dry.released()); CHECK(!f.group.released.load());
    CHECK(f.engine.recording()); CHECK(f.engine.post({EngineCommand::Type::Stop})); process(f.engine,io);
    CHECK(f.group.released.load()); f.wet.wait(); f.dry.wait();
    CHECK(f.wet.audio()->frames==128); CHECK(f.dry.audio()->frames==256); CHECK(f.engine.position()==0);
}
TEST(multirecording, missing_input_stops_all_takes_and_pending_start_is_safe_on_shutdown) {
    {
        Fixture f; f.start();
        float a[64]{},left[64]{},right[64]{}; float* in[]{a}; float* out[]{left,right};
        process(f.engine,{{in,1,64},{out,2,64},nullptr,64});
        CHECK(f.group.released.load()); CHECK(f.group.inputLost.load()); CHECK(!f.engine.recording()); CHECK(!f.engine.playing());
        f.wet.wait(); f.dry.wait(); CHECK(f.wet.audio()->frames==0 && f.dry.audio()->frames==0);
    }
    {
        Fixture f; f.start(); f.engine.stop();
        CHECK(f.group.released.load()); f.wet.wait(); f.dry.wait(); CHECK(f.wet.audio()->frames==0 && f.dry.audio()->frames==0);
    }
}
TEST(multirecording, a_failed_writer_stops_the_whole_group_and_preserves_completed_samples) {
    Fixture f;
    float a[128]{},b[128]{},left[128]{},right[128]{}; float* in[]{a,b}; float* out[]{left,right};
    CHECK(f.dry.capture(b,8));
    CHECK(!f.dry.capture(b,(1<<18)+1)); // oversized block is rejected before reading the source
    f.start(); process(f.engine,{{in,2,128},{out,2,128},nullptr,128});
    CHECK(f.group.released.load()); CHECK(f.group.captureFailed.load()); CHECK(!f.engine.recording());
    f.wet.wait(); f.dry.wait(); CHECK(!f.dry.error().empty());
    CHECK(f.dry.audio()->frames==8); CHECK(f.wet.audio()->frames==128);
}
