#include "testkit.h"
#include "engine/audioengine.h"
#include <filesystem>
#include <unistd.h>

namespace {
void process(AudioEngine& engine, const AudioIO& io) noexcept RT_SAFE { engine.renderBlock(io); }
class StereoGain final : public Processor {
public:
    std::string typeId() const override { return "test.stereoGain"; }
    void prepare(double, int) override {}
    void reset() noexcept override {}
    void process(ProcessContext& context) noexcept override {
        for (int c = 0; c < context.audio.channelCount; ++c)
            for (int i = 0; i < context.audio.frames; ++i)
                context.audio.channel(c)[i] *= c == 0 ? 2.0f : -3.0f;
    }
};
struct Fixture {
    AudioEngine engine{nullptr};
    float input[128]{}, left[128]{}, right[128]{};
    float* in[1]{input}; float* out[2]{left, right};
    AudioIO io{{in,1,128},{out,2,128},nullptr,128};
    std::string path = "/tmp/chaosmaker-recording-" + std::to_string(getpid()) + ".wav";
    Fixture() { engine.deviceChanged(48000,128); }
    ~Fixture() { engine.stop(); std::filesystem::remove(path); }
    void command(EngineCommand::Type type, std::int64_t a=0, std::int64_t b=0) { CHECK(engine.post({type,a,b})); }
    void run() { process(engine,io); }
};
}
TEST(recording, take_is_dry_aligned_and_playable_after_pause_and_stop) {
    Fixture f; auto take=std::make_unique<TakeRecorder>(f.path,48000);
    for(int i=0;i<128;++i) f.input[i]=float(i)/512;
    f.command(EngineCommand::Type::Seek,480);
    f.command(EngineCommand::Type::SetCleanMonitor,1);
    CHECK(f.engine.setInputTrimDb(-24));
    EngineCommand record; record.type=EngineCommand::Type::StartRecording; record.recorder=take.get();
    CHECK(f.engine.post(record)); f.run(); f.run();
    CHECK(f.engine.position()==736); CHECK(f.engine.recording());
    f.command(EngineCommand::Type::Pause); f.run();
    CHECK(!f.engine.playing()); CHECK(f.engine.position()==736);
    CHECK(f.engine.releasedRecorder()==take.get()); take->wait();
    CHECK(take->error().empty()); CHECK(take->startSample==480);
    auto audio=loadTakeWav(f.path); CHECK(audio->frames==256);
    for(int i=0;i<256;++i) CHECK(audio->channels[0][i]==f.input[i%128]);
    auto snapshot=std::make_unique<EngineSnapshot>(); TrackRender track;
    track.clips.push_back({audio.get(),480,256}); snapshot->tracks.push_back(track); snapshot->keepAudio.push_back(audio);
    CHECK(f.engine.tryPublishPlayback(snapshot));
    f.command(EngineCommand::Type::SetCleanMonitor,0); f.command(EngineCommand::Type::Seek,480);
    f.command(EngineCommand::Type::Play); f.run();
    for(int i=0;i<128;++i) { CHECK_NEAR(f.left[i],f.input[i],1e-6); CHECK(f.left[i]==f.right[i]); }
    f.command(EngineCommand::Type::Stop); f.run(); CHECK(f.engine.position()==0);
    for(float v:f.left) CHECK(v==0);
}
TEST(recording, clip_boundaries_mute_solo_volume_and_loop_are_sample_accurate) {
    Fixture f; auto audio=std::make_shared<AudioData>(); audio->frames=10; audio->channels={{0.2f,0.3f,0.4f,0.5f,0.6f,0.7f,0.8f,0.9f,1,1}};
    auto publish=[&](bool muted, bool solo) {
        auto s=std::make_unique<EngineSnapshot>(); TrackRender a,b;
        a.gain=0.5f; a.muted=muted; a.soloed=solo; a.clips.push_back({audio.get(),3,4,1});
        b.clips.push_back({audio.get(),3,4,0}); s->tracks={a,b}; s->keepAudio.push_back(audio);
        CHECK(f.engine.tryPublishPlayback(s));
    };
    publish(false,true); f.command(EngineCommand::Type::Play); f.run();
    CHECK(f.left[2]==0); CHECK_NEAR(f.left[3],0.15,1e-6); CHECK_NEAR(f.left[6],0.3,1e-6); CHECK(f.left[7]==0);
    publish(true,true); f.command(EngineCommand::Type::Seek,0); f.run(); for(float v:f.left) CHECK(v==0);
    publish(false,false); f.command(EngineCommand::Type::Seek,3); f.command(EngineCommand::Type::SetLoop,3,7); f.run();
    CHECK_NEAR(f.left[0],0.35,1e-6); CHECK(f.left[0]==f.left[4]); CHECK(f.engine.position()==7);
    f.command(EngineCommand::Type::Pause); f.run(); CHECK(f.engine.position()==7);
}
TEST(recording, overflow_preserves_prefix_and_shutdown_finishes_queued_take) {
    Fixture f;
    auto take=std::make_unique<TakeRecorder>(f.path,48000);
    CHECK(take->capture(f.input,128));
    CHECK(!take->capture(f.input,(1<<18)+1)); // rejected before dereferencing the oversized span
    take->wait(); CHECK(!take->error().empty()); CHECK(loadTakeWav(f.path)->frames==128);
    take.reset(); take=std::make_unique<TakeRecorder>(f.path,48000);
    EngineCommand record; record.type=EngineCommand::Type::StartRecording; record.recorder=take.get();
    CHECK(f.engine.post(record)); f.engine.stop(); take->wait(); CHECK(take->done());
    CHECK(f.engine.releasedRecorder()==take.get()); CHECK(take->audio()->frames==0);
}
TEST(recording, mute_silences_monitor_and_playback_runs_without_input) {
    Fixture f; f.command(EngineCommand::Type::SetCleanMonitor,1);
    std::fill_n(f.input,128,0.3f);
    EngineCommand gain; gain.type=EngineCommand::Type::SetCleanMonitorGain; gain.value=0; CHECK(f.engine.post(gain));
    f.run(); for(float v:f.left) CHECK(v==0);
    f.io.input={nullptr,0,0}; f.command(EngineCommand::Type::Play); f.run(); CHECK(f.engine.position()==128);
}
TEST(recording, variable_callback_sizes_keep_dry_audio_contiguous) {
    Fixture f; auto take=std::make_unique<TakeRecorder>(f.path,48000);
    EngineCommand record; record.type=EngineCommand::Type::StartRecording; record.recorder=take.get(); CHECK(f.engine.post(record));
    std::vector<float> expected;
    for(int size : {17,257,64,1024,3}) {
        std::vector<float> input(size), output(size);
        for(int i=0;i<size;++i) input[i]=float(std::sin(double(expected.size()+i)*0.1)*0.2);
        expected.insert(expected.end(),input.begin(),input.end());
        float* in[]{input.data()}; float* out[]{output.data()}; AudioIO io{{in,1,size},{out,1,size},nullptr,size};
        process(f.engine,io);
    }
    f.command(EngineCommand::Type::Stop); f.run(); take->wait();
    CHECK(loadTakeWav(f.path)->channels[0]==expected); CHECK(take->startSample==0);
    CHECK(std::filesystem::file_size(f.path)==56+expected.size()*4);
}
TEST(recording, latest_playback_edit_replaces_an_unconsumed_snapshot) {
    Fixture f; auto audio=std::make_shared<AudioData>(); audio->frames=128; audio->channels={std::vector<float>(128,0.3f)};
    f.engine.publishPlayback(std::make_unique<EngineSnapshot>());
    auto snapshot=std::make_unique<EngineSnapshot>(); TrackRender track;
    track.clips.push_back({audio.get(),0,128}); snapshot->tracks.push_back(track); snapshot->keepAudio.push_back(audio);
    f.engine.publishPlayback(std::move(snapshot));
    f.command(EngineCommand::Type::Play); f.run();
    for(float v:f.left) CHECK_NEAR(v,0.3,1e-6);
}

TEST(recording, wet_take_preserves_stereo_trim_and_excludes_monitor_gain_and_backing_tracks) {
    Fixture f;
    CHECK(f.engine.setInputTrimDb(-6.020599913));
    f.engine.deviceChanged(48000,128); // settle trim at exactly half gain
    auto fx=std::make_shared<StereoGain>(); auto effects=std::make_unique<EngineSnapshot>();
    effects->masterChain.push_back(fx.get()); effects->keepProcessors.push_back(fx);
    CHECK(f.engine.tryPublish(effects));
    auto backing=std::make_shared<AudioData>(); backing->frames=4096;
    backing->channels={std::vector<float>(4096,0.4f)};
    auto playback=std::make_unique<EngineSnapshot>(); TrackRender track;
    track.clips.push_back({backing.get(),0,4096}); playback->tracks.push_back(track); playback->keepAudio.push_back(backing);
    CHECK(f.engine.tryPublishPlayback(playback));
    auto take=std::make_unique<TakeRecorder>(f.path,48000,TakeRecorder::Source::PostFx);
    f.command(EngineCommand::Type::Seek,480);
    EngineCommand record; record.type=EngineCommand::Type::StartRecording; record.recorder=take.get();
    CHECK(f.engine.post(record));
    std::vector<float> expected;
    int block=0;
    for(int size : {17,257,64,1024,3}) {
        // Test disabled monitoring, enabled monitoring at zero gain, and audible monitoring.
        f.command(EngineCommand::Type::SetCleanMonitor,block != 0);
        EngineCommand gain; gain.type=EngineCommand::Type::SetCleanMonitorGain;
        gain.value=block < 2 ? 0.0f : 0.25f; CHECK(f.engine.post(gain));
        std::vector<float> input(size), left(size), right(size);
        for(int i=0;i<size;++i) input[i]=float(std::sin(double(expected.size()+i)*0.1)*0.2);
        expected.insert(expected.end(),input.begin(),input.end());
        float* in[]{input.data()}; float* out[]{left.data(),right.data()};
        process(f.engine,{{in,1,size},{out,2,size},nullptr,size});
        for(int i=0;i<size;++i) {
            CHECK_NEAR(left[i],0.4f+input[i]*gain.value,1e-6);
            CHECK_NEAR(right[i],0.4f-input[i]*1.5f*gain.value,1e-6);
        }
        ++block;
    }
    f.command(EngineCommand::Type::Pause); f.run(); take->wait();
    CHECK(take->error().empty()); CHECK(take->startSample==480);
    auto audio=loadTakeWav(f.path); CHECK(audio->frames==std::int64_t(expected.size()));
    CHECK(audio->channels.size()==2); CHECK(audio->channels==take->audio()->channels);
    CHECK(std::filesystem::file_size(f.path)==56+expected.size()*8);
    for(std::size_t i=0;i<expected.size();++i) {
        CHECK_NEAR(audio->channels[0][i],expected[i],1e-6);
        CHECK_NEAR(audio->channels[1][i],-expected[i]*1.5f,1e-6);
    }
    // Existing playback must not run the printed take through the active FX again.
    playback=std::make_unique<EngineSnapshot>(); track.clips={{audio.get(),480,audio->frames}};
    playback->tracks.push_back(track); playback->keepAudio.push_back(audio);
    CHECK(f.engine.tryPublishPlayback(playback));
    f.command(EngineCommand::Type::SetCleanMonitor,0);
    f.command(EngineCommand::Type::Seek,480); f.command(EngineCommand::Type::Play); f.run();
    for(int i=0;i<128;++i) {
        CHECK_NEAR(f.left[i],audio->channels[0][i],1e-6);
        CHECK_NEAR(f.right[i],audio->channels[1][i],1e-6);
    }
}
TEST(recording, stereo_overflow_keeps_complete_frames_and_missing_input_finishes_take) {
    Fixture f; auto take=std::make_unique<TakeRecorder>(f.path,48000,TakeRecorder::Source::PostFx);
    std::fill_n(f.left,128,0.2f); std::fill_n(f.right,128,-0.3f);
    CHECK(take->capture({f.out,2,128}));
    CHECK(!take->capture({f.out,2,(1<<18)+1}));
    take->wait(); CHECK(!take->error().empty());
    auto audio=loadTakeWav(f.path); CHECK(audio->frames==128);
    CHECK(audio->channels[0][127]==0.2f); CHECK(audio->channels[1][127]==-0.3f);
    take=std::make_unique<TakeRecorder>(f.path,48000,TakeRecorder::Source::PostFx);
    EngineCommand record; record.type=EngineCommand::Type::StartRecording; record.recorder=take.get();
    CHECK(f.engine.post(record)); f.run();
    f.io.input={nullptr,0,0}; f.run(); take->wait();
    CHECK(!f.engine.recording()); CHECK(!f.engine.playing()); CHECK(take->audio()->frames==128);
}
TEST(recording, planar_ring_wrap_keeps_channels_and_rejects_partial_blocks) {
    RtAudioRing<8> ring;
    float left[]{1,2,3}, right[]{4,5,6}; float* channels[]{left,right}; float out[6]{};
    CHECK(ring.pushPlanar(channels,2,3)); CHECK(!ring.pushPlanar(channels,2,2));
    CHECK(ring.pop({out,4})); CHECK(out[0]==1 && out[1]==4 && out[2]==2 && out[3]==5);
    CHECK(ring.pushPlanar(channels,2,2)); CHECK(ring.pop({out,6}));
    const float expected[]{3,6,1,4,2,5};
    for(int i=0;i<6;++i) CHECK(out[i]==expected[i]);
}

TEST(recording, dry_playback_uses_track_inserts_once_and_printed_clips_bypass_them) {
    Fixture f;
    auto audio=std::make_shared<AudioData>(); audio->frames=256; audio->channels={std::vector<float>(256,0.1f)};
    auto fx=std::make_shared<StereoGain>();
    auto snapshot=std::make_unique<EngineSnapshot>(); TrackRender track;
    ClipRender dry{audio.get(),0,256}; dry.effectsPrinted=false;
    ClipRender printed{audio.get(),0,256};
    track.clips={dry,printed}; track.chain={fx.get()}; track.gain=0.5f;
    snapshot->tracks.push_back(track); snapshot->keepAudio.push_back(audio); snapshot->keepProcessors.push_back(fx);
    f.engine.publishPlayback(std::move(snapshot)); f.command(EngineCommand::Type::Play); f.run();
    for(int i=0;i<128;++i) { CHECK_NEAR(f.left[i],0.15,1e-6); CHECK_NEAR(f.right[i],-0.1,1e-6); }
    f.command(EngineCommand::Type::SetLoop,0,17); f.command(EngineCommand::Type::Seek,0); f.run();
    CHECK(f.engine.position()==9); for(float sample:f.left)CHECK_NEAR(sample,0.15,1e-6);
}
