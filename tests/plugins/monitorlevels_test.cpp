#include "testkit.h"
#include "engine/audioengine.h"
#include "testsignals.h"
#include <limits>

namespace {
void process(AudioEngine& engine, const AudioIO& io) noexcept RT_SAFE { engine.renderBlock(io); }
void arm(AudioEngine& engine, bool armed) {
    EngineCommand command; command.type=EngineCommand::Type::SetCleanMonitor; command.a=armed;
    CHECK(engine.post(command));
}
class Boost final : public Processor {
public:
    float dryPeak = 0;
    std::string typeId() const override { return "test.boost"; }
    void prepare(double, int) override {}
    void reset() noexcept override {}
    void analyseInput(ProcessContext& context) noexcept override { dryPeak = context.audio.channel(0)[0]; }
    void process(ProcessContext& ctx) noexcept override {
        for (int c=0;c<ctx.audio.channelCount;++c)
            for(int i=0;i<ctx.audio.frames;++i) ctx.audio.channel(c)[i] *= c==0 ? 2 : 4;
    }
};
}
TEST(monitorlevels, trim_changes_monitor_levels_but_preserves_dry_input) {
    AudioEngine engine(nullptr); engine.deviceChanged(48000,128); arm(engine,true);
    auto boost=std::make_shared<Boost>(); auto snapshot=std::make_unique<EngineSnapshot>();
    snapshot->masterChain.push_back(boost.get()); snapshot->keepProcessors.push_back(boost);
    engine.publish(std::move(snapshot));
    CHECK(engine.setInputTrimDb(-6.020599913));
    float input[128], left[128]{}, right[128]{};
    std::fill_n(input,128,0.25f); float* in[]{input}; float* out[]{left,right};
    AudioIO io{{in,1,128},{out,2,128},nullptr,128};
    // Let the input trim settle, then measure a fresh polling interval.
    for (int i=0;i<400;++i) process(engine,io);
    engine.takeMonitorLevels(); process(engine,io);
    auto levels=engine.takeMonitorLevels();
    CHECK_NEAR(levels.raw,0.25,1e-6); CHECK_NEAR(levels.preFx,0.125,1e-5);
    CHECK_NEAR(levels.left,0.25,2e-5); CHECK_NEAR(levels.right,0.5,4e-5);
    CHECK_NEAR(boost->dryPeak,0.25,1e-6); CHECK(input[0]==0.25f);
    CHECK(engine.takeMonitorLevels().raw==0);
    arm(engine,false); process(engine,io); levels=engine.takeMonitorLevels();
    CHECK(levels.raw==0.25f); CHECK(levels.preFx==0 && levels.left==0 && levels.right==0);
}
TEST(monitorlevels, brief_peaks_and_clipping_are_retained_until_consumed_or_cleared) {
    PeakMeter meter;
    float samples[]{1.1f,0,0,0}; float* channels[]{samples};
    meter.capture({channels,1,4}); samples[0]=0.1f; meter.capture({channels,1,4});
    CHECK_NEAR(meter.takePeak(),1.1,1e-6); CHECK(meter.clipped());
    CHECK(meter.takePeak()==0); CHECK(meter.clipped());
    meter.clearClip(); CHECK(!meter.clipped());
    samples[0]=0.99997f; meter.capture({channels,1,4}); CHECK(meter.clipped());
}
TEST(monitorlevels, trim_changes_are_smooth_and_invalid_controls_are_rejected) {
    AudioEngine engine(nullptr); engine.deviceChanged(48000,128); arm(engine,true);
    float input[128], output[128]{}; std::fill_n(input,128,0.1f);
    float* in[]{input}; float* out[]{output}; AudioIO io{{in,1,128},{out,1,128},nullptr,128};
    process(engine,io); const float previous=output[127];
    CHECK(engine.setInputTrimDb(24)); process(engine,io);
    CHECK(std::abs(output[0]-previous)<0.002);
    for(int i=1;i<128;++i) CHECK(std::abs(output[i]-output[i-1])<0.002);
    for(double db : {-25.0,25.0,std::numeric_limits<double>::quiet_NaN()}) CHECK(!engine.setInputTrimDb(db));
    CHECK(engine.setInputTrimDb(0));
}
