#include "testkit.h"
#include "engine/audioengine.h"

namespace {
void process(AudioEngine& engine, const AudioIO& io) noexcept RT_SAFE { engine.renderBlock(io); }
void arm(AudioEngine& engine, bool enabled) {
    CHECK(engine.post({EngineCommand::Type::SetCleanMonitor,enabled}));
}
class Boost final : public Processor {
public:
    std::string typeId() const override { return "test.stereoMeterBoost"; }
    void prepare(double, int) override {}
    void reset() noexcept override {}
    void process(ProcessContext& context) noexcept override {
        for(int c=0;c<context.audio.channelCount;++c)
            for(int i=0;i<context.audio.frames;++i) context.audio.channel(c)[i]*=c==0 ? 2.0f : 4.0f;
    }
};
}

TEST(trackmeters, stereo_playback_separates_input_fx_fader_and_solo_without_changing_printed_audio) {
    AudioEngine engine(nullptr); engine.deviceChanged(48000,128);
    auto meters=std::make_shared<TrackMeters>(), other=std::make_shared<TrackMeters>();
    auto printed=std::make_shared<AudioData>(), dry=std::make_shared<AudioData>();
    printed->frames=dry->frames=4096;
    printed->channels={std::vector<float>(4096,0.2f),std::vector<float>(4096,0.4f)};
    dry->channels={std::vector<float>(4096,0.1f)};
    auto boost=std::make_shared<Boost>();
    auto publish=[&](bool muted) {
        auto snapshot=std::make_unique<EngineSnapshot>(); TrackRender a,b;
        a.meters=meters; a.gain=0.5f; a.soloed=true; a.muted=muted;
        a.chain.push_back(boost.get()); a.clips.push_back({printed.get(),0,4096});
        ClipRender clip; clip.audio=dry.get(); clip.length=4096; clip.effectsPrinted=false; a.clips.push_back(clip);
        b.meters=other; b.clips.push_back({printed.get(),0,4096});
        snapshot->tracks={a,b}; snapshot->keepAudio={printed,dry}; snapshot->keepProcessors.push_back(boost);
        engine.publishPlayback(std::move(snapshot));
    };
    publish(false); CHECK(engine.post({EngineCommand::Type::Play}));
    std::vector<float> left(2053),right(2053); float* out[]{left.data(),right.data()};
    AudioIO io{{nullptr,0,0},{out,2,2053},nullptr,2053}; process(engine,io);
    CHECK_NEAR(meters->input[0].takePeak(),0.3,1e-6); CHECK_NEAR(meters->input[1].takePeak(),0.5,1e-6);
    CHECK_NEAR(meters->output[0].takePeak(),0.2,1e-6); CHECK_NEAR(meters->output[1].takePeak(),0.4,1e-6);
    CHECK_NEAR(other->input[1].takePeak(),0.4,1e-6); CHECK(other->output[1].takePeak()==0);
    for(int i=0;i<2053;++i) { CHECK_NEAR(left[i],0.2,1e-6); CHECK_NEAR(right[i],0.4,1e-6); }
    publish(true); CHECK(engine.post({EngineCommand::Type::Seek,0})); process(engine,io);
    CHECK_NEAR(meters->input[0].takePeak(),0.3,1e-6); CHECK(meters->output[0].takePeak()==0);
    for(float sample:left)CHECK(sample==0);
}

TEST(trackmeters, live_and_playback_sum_on_the_correct_track_and_clips_clear_independently) {
    AudioEngine engine(nullptr); engine.deviceChanged(48000,128); arm(engine,true);
    auto meters=std::make_shared<TrackMeters>(), other=std::make_shared<TrackMeters>();
    auto monitor=std::make_unique<EngineSnapshot>(); monitor->monitorMeters=meters;
    engine.publishMonitor(std::move(monitor));
    float input[128],left[128]{},right[128]{}; std::fill_n(input,128,0.25f);
    float* in[]{input}; float* out[]{left,right}; AudioIO io{{in,1,128},{out,2,128},nullptr,128};
    process(engine,io); CHECK_NEAR(meters->input[0].takePeak(),0.25,1e-6);
    CHECK_NEAR(meters->output[1].takePeak(),0.25,1e-6);
    auto audio=std::make_shared<AudioData>(); audio->frames=4096;
    audio->channels={std::vector<float>(4096,0.8f)};
    auto playback=std::make_unique<EngineSnapshot>(); TrackRender a,b;
    a.meters=other; a.clips.push_back({audio.get(),0,4096}); a.gain=0.1f;
    b.meters=meters; b.clips.push_back({audio.get(),0,4096});
    playback->tracks={a,b}; playback->keepAudio.push_back(audio); engine.publishPlayback(std::move(playback));
    CHECK(engine.post({EngineCommand::Type::Play})); process(engine,io);
    CHECK_NEAR(meters->input[0].takePeak(),1.05,1e-6);
    CHECK_NEAR(meters->output[0].takePeak(),1.05,1e-6);
    CHECK_NEAR(other->output[0].takePeak(),0.08,1e-6); // excludes the monitored track
    CHECK(meters->output[0].clipped()); CHECK(!other->output[0].clipped());
    meters->clearClips(); CHECK(!meters->output[0].clipped());
    CHECK(meters->output[0].takePeak()==0);
    CHECK(engine.post({EngineCommand::Type::Stop})); arm(engine,false); process(engine,io);
    CHECK(meters->output[0].takePeak()==0);
}

TEST(trackmeters, track_pan_scales_each_output_channel_on_top_of_the_fader) {
    AudioEngine engine(nullptr); engine.deviceChanged(48000,128);
    auto meters=std::make_shared<TrackMeters>();
    auto audio=std::make_shared<AudioData>();
    audio->frames=4096; audio->channels={std::vector<float>(4096,0.2f),std::vector<float>(4096,0.4f)};
    auto publish=[&](float left,float right) {
        auto snapshot=std::make_unique<EngineSnapshot>(); TrackRender track;
        track.meters=meters; track.gain=0.5f; track.panLeft=left; track.panRight=right;
        track.clips.push_back({audio.get(),0,4096});
        snapshot->tracks={track}; snapshot->keepAudio={audio};
        engine.publishPlayback(std::move(snapshot));
    };
    std::vector<float> left(512),right(512); float* out[]{left.data(),right.data()};
    AudioIO io{{nullptr,0,0},{out,2,512},nullptr,512};
    publish(1.0f,0.25f); CHECK(engine.post({EngineCommand::Type::Play})); process(engine,io);
    for(int i=0;i<512;++i) { CHECK_NEAR(left[i],0.1,1e-6); CHECK_NEAR(right[i],0.05,1e-6); }
    CHECK_NEAR(meters->output[0].takePeak(),0.1,1e-6); CHECK_NEAR(meters->output[1].takePeak(),0.05,1e-6);
    CHECK_NEAR(meters->input[1].takePeak(),0.4,1e-6);            // input meter is before the pan
    std::fill(left.begin(),left.end(),0.0f); std::fill(right.begin(),right.end(),0.0f);
    publish(0.0f,1.0f); process(engine,io);                     // hard right
    for(int i=0;i<512;++i) { CHECK(left[i]==0.0f); CHECK_NEAR(right[i],0.2,1e-6); }
}
