#include "testkit.h"
#include "testsignals.h"
#include "plugins/crunch/crunch.h"
#include "plugins/distortion/distortion.h"
#include "plugins/guitarsuite/guitarsuite.h"
#include "engine/audioengine.h"
#include <memory>

namespace {
void set(Processor& processor, const char* id, float value) {
    for (const auto& parameter : processor.parameters())
        if (parameter->id == id) parameter->set(value);
}
double tailRms(const std::vector<float>& audio) {
    return testsignals::rmsDb({audio.begin() + audio.size()/2, audio.end()});
}
void process(AudioEngine& engine, const AudioIO& io) noexcept RT_SAFE { engine.renderBlock(io); }
}

TEST(drives, crunch_and_distortion_have_distinct_saturation_and_touch_response) {
    Crunch crunch; Distortion distortion;
    const auto soft = testsignals::sine(220, -30, 24000);
    const auto loud = testsignals::sine(220, -12, 24000);
    const auto crunchSoft = testsignals::render(crunch, soft, 128);
    const auto crunchLoud = testsignals::render(crunch, loud, 128);
    const auto distortionSoft = testsignals::render(distortion, soft, 128);
    const auto distortionLoud = testsignals::render(distortion, loud, 128);
    CHECK(crunchLoud != distortionLoud);
    CHECK(crunchLoud != loud);
    CHECK(tailRms(crunchLoud) - tailRms(crunchSoft) > tailRms(distortionLoud) - tailRms(distortionSoft) + 5);
    set(crunch, "drive", 0);
    const auto low = testsignals::render(crunch, loud, 128);
    set(crunch, "drive", 30);
    CHECK(testsignals::render(crunch, loud, 128) != low);
}

TEST(drives, distortion_tight_reduces_low_frequency_input_before_clipping) {
    Distortion distortion; set(distortion, "drive", 6); set(distortion, "tight", 0);
    const auto input = testsignals::sine(40, -48, 48000);
    const double loose = tailRms(testsignals::render(distortion, input, 128));
    set(distortion, "tight", 1);
    CHECK(tailRms(testsignals::render(distortion, input, 128)) < loose - 7);
}

TEST(drives, distortion_mid_gain_and_frequency_shape_the_post_clipping_signal) {
    Distortion distortion; set(distortion, "drive", 6);
    const auto input = testsignals::sine(750, -42, 24000);
    const double neutral = tailRms(testsignals::render(distortion, input, 128));
    set(distortion, "mid", -12);
    const double cut = tailRms(testsignals::render(distortion, input, 128));
    CHECK_NEAR(cut, neutral - 12, 0.2);
    set(distortion, "midfreq", 300);
    CHECK(tailRms(testsignals::render(distortion, input, 128)) > cut + 6);
}

TEST(drives, crunch_tone_and_body_change_brightness_and_low_end) {
    Crunch crunch; set(crunch, "drive", 0); set(crunch, "tone", 0);
    const auto high = testsignals::sine(4000, -42, 24000);
    const double dark = tailRms(testsignals::render(crunch, high, 128));
    set(crunch, "tone", 1);
    CHECK(tailRms(testsignals::render(crunch, high, 128)) > dark + 6);
    const auto low = testsignals::sine(70, -42, 24000);
    set(crunch, "body", -6);
    const double thin = tailRms(testsignals::render(crunch, low, 128));
    set(crunch, "body", 6);
    CHECK(tailRms(testsignals::render(crunch, low, 128)) > thin + 8);
}

TEST(drives, output_level_is_independent_of_drive_and_changes_are_smoothed) {
    Crunch crunch; Distortion distortion;
    for (Processor* processor : std::vector<Processor*>{&crunch, &distortion}) {
        const auto input = testsignals::sine(220, -24, 10000);
        set(*processor, "level", -6);
        const double before = tailRms(testsignals::render(*processor, input, 128));
        set(*processor, "level", -12);
        CHECK_NEAR(tailRms(testsignals::render(*processor, input, 128)), before - 6, 0.01);
        processor->prepare(48000, 128);
        auto output = input;
        for (int position = 0; position < int(output.size()); position += 128) {
            if (position == 4096) set(*processor, "level", 6);
            float* channels[]{output.data() + position}; ProcessContext context;
            context.audio = {channels, 1, std::min(128, int(output.size()) - position)};
            testsignals::rtProcess(*processor, context);
        }
        CHECK(std::abs(output[4096] - output[4095]) < 0.03);
    }
}

TEST(drives, processors_are_block_independent_finite_and_keep_silent_channels_silent) {
    Crunch crunch; Distortion distortion;
    for (Processor* processor : std::vector<Processor*>{&crunch, &distortion}) {
        const auto input = testsignals::sine(220, -18, 12000);
        const auto reference = testsignals::render(*processor, input, 128);
        CHECK(testsignals::render(*processor, input, 37) == reference);
        CHECK(testsignals::render(*processor, input, 257) == reference);
        for (const auto& parameter : processor->parameters()) parameter->set(parameter->max);
        for (double rate : {44100.0, 48000.0, 96000.0}) {
            processor->prepare(rate, 128);
            for (int block = 0; block < 32; ++block) {
                float left[128], right[128]{};
                for (int i = 0; i < 128; ++i) left[i] = float(0.25 * std::sin(testsignals::kTwoPi * 220 * (block*128+i) / rate));
                float* channels[]{left,right}; ProcessContext context;
                context.audio = {channels, 2, 128}; context.sampleRate = rate;
                testsignals::rtProcess(*processor, context);
                for (int i = 0; i < 128; ++i) { CHECK(std::isfinite(left[i]) && std::abs(left[i]) < 64); CHECK(right[i] == 0); }
            }
        }
        CHECK(testsignals::peakDb(testsignals::render(*processor, std::vector<float>(6000), 128)) < -110);
        CHECK(tailRms(testsignals::render(*processor, std::vector<float>(48000, 0.1f), 128)) < -90);
    }
}

TEST(drives, live_monitor_processes_drive_pedals_without_recording_and_bypass_returns_dry) {
    AudioEngine engine(nullptr); engine.deviceChanged(48000, 128);
    auto crunch = std::make_shared<PedalSlot>(); crunch->processor = std::make_shared<Crunch>();
    auto distortion = std::make_shared<PedalSlot>(); distortion->processor = std::make_shared<Distortion>();
    auto chain = std::make_shared<GuitarSuite>(std::vector<std::shared_ptr<PedalSlot>>{crunch,distortion});
    chain->prepare(48000,128);
    auto snapshot = std::make_unique<EngineSnapshot>(); snapshot->masterChain.push_back(chain.get()); snapshot->keepProcessors.push_back(chain);
    engine.publish(std::move(snapshot));
    EngineCommand arm; arm.type = EngineCommand::Type::SetCleanMonitor; arm.a = 1; CHECK(engine.post(arm));
    auto input = testsignals::sine(220,-18,128); float left[128]{}, right[128]{};
    float* in[]{input.data()}; float* out[]{left,right}; AudioIO io{{in,1,128},{out,2,128},nullptr,128};
    process(engine,io);
    bool changed = false;
    for (int i = 0; i < 128; ++i) { changed |= std::abs(left[i] - input[i]) > 0.01; CHECK(left[i] == right[i]); }
    CHECK(changed);
    crunch->bypassed.store(true); distortion->bypassed.store(true);
    for (int block = 0; block < 60; ++block) process(engine,io);
    for (int i = 0; i < 128; ++i) CHECK_NEAR(left[i],input[i],1e-5);
    arm.a = 0; CHECK(engine.post(arm)); process(engine,io);
    for (float sample : left) CHECK(sample == 0);
}
