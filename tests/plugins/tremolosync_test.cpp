#include "testkit.h"
#include "testsignals.h"
#include "plugins/tremolo/tremolo.h"
#include "engine/audioengine.h"
#include <limits>

namespace {
void set(Processor& p, const char* id, float value) {
    for (const auto& param : p.parameters()) if (param->id == id) param->set(value);
}
std::vector<float> render(Tremolo& tremolo, double bpm, int block, bool reset = true) {
    if (reset) { tremolo.prepare(48000, block); tremolo.reset(); }
    std::vector<float> audio(48000 * 6, 1);
    for (int i = 0; i < int(audio.size()); i += block) {
        float* channels[]{audio.data() + i}; ProcessContext ctx;
        ctx.audio = {channels, 1, std::min(block, int(audio.size()) - i)}; ctx.bpm = bpm;
        testsignals::rtProcess(tremolo, ctx);
    }
    return audio;
}
int pulses(const std::vector<float>& audio) {
    int count = 0;
    for (std::size_t i = 1; i < audio.size(); ++i)
        if (audio[i-1] >= 0.5f && audio[i] < 0.5f) ++count;
    return count;
}
class TempoProbe final : public Processor {
public:
    double seenBpm = 0;
    std::string typeId() const override { return "test.tempo"; }
    void prepare(double, int) override {}
    void reset() noexcept override {}
    void process(ProcessContext& ctx) noexcept override { seenBpm = ctx.bpm; }
};
void engineProcess(AudioEngine& engine, const AudioIO& io) noexcept RT_SAFE { engine.renderBlock(io); }
}

TEST(tremolosync, divisions_follow_bpm_and_manual_rate_is_independent) {
    Tremolo tremolo;
    set(tremolo, "sync", 1); set(tremolo, "depth", 1);
    const double beats[]{4, 2, 1, 0.5, 0.25, 0.75, 1.0/3};
    for (int division = 0; division < 7; ++division) {
        set(tremolo, "division", float(division));
        for (double bpm : {60.0, 120.0, 180.0}) {
            const double expected = 6 * bpm / (60 * beats[division]);
            CHECK(std::abs(pulses(render(tremolo, bpm, 128)) - expected) <= 0.51);
        }
    }
    set(tremolo, "sync", 0); set(tremolo, "rate", 4);
    CHECK(render(tremolo, 60, 128) == render(tremolo, 180, 128));
    CHECK(pulses(render(tremolo, 120, 128)) == 24);
}
TEST(tremolosync, synced_processing_is_block_independent_and_rate_changes_are_smooth) {
    Tremolo tremolo; set(tremolo, "sync", 1); set(tremolo, "depth", 1);
    const auto reference = render(tremolo, 97.5, 128);
    CHECK(render(tremolo, 97.5, 257) == reference);
    const auto changed = render(tremolo, 180, 64, false);
    CHECK(std::abs(changed.front() - reference.back()) < 0.01);
    for (std::size_t i = 1; i < changed.size(); ++i)
        CHECK(std::isfinite(changed[i]) && std::abs(changed[i] - changed[i-1]) < 0.01);
    for (double bpm : {0.01, 10000.0, 0.0, std::numeric_limits<double>::quiet_NaN()}) {
        const auto audio = render(tremolo, bpm, 128);
        for (float sample : audio) CHECK(std::isfinite(sample) && sample >= 0 && sample <= 1);
    }
}
TEST(tremolosync, engine_delivers_latest_tempo_across_snapshot_changes) {
    AudioEngine engine(nullptr); engine.deviceChanged(48000, 128);
    auto probe = std::make_shared<TempoProbe>();
    auto publish = [&] {
        auto snapshot = std::make_unique<EngineSnapshot>();
        snapshot->masterChain.push_back(probe.get()); snapshot->keepProcessors.push_back(probe);
        engine.publish(std::move(snapshot));
    };
    publish();
    EngineCommand monitor; monitor.type = EngineCommand::Type::SetCleanMonitor; monitor.a = 1;
    CHECK(engine.post(monitor));
    float input[128]{}, output[128]{}; float* in[]{input}; float* out[]{output};
    AudioIO io{{in, 1, 128}, {out, 1, 128}, nullptr, 128};
    engineProcess(engine, io); CHECK_NEAR(probe->seenBpm, 120, 1e-9);
    CHECK(engine.setTempoBpm(97.5)); engineProcess(engine, io); CHECK_NEAR(probe->seenBpm, 97.5, 1e-9);
    publish(); engineProcess(engine, io); CHECK_NEAR(probe->seenBpm, 97.5, 1e-9);
    for (double bad : {0.0, -10.0, 10001.0, std::numeric_limits<double>::quiet_NaN()})
        CHECK(!engine.setTempoBpm(bad));
    engineProcess(engine, io); CHECK_NEAR(probe->seenBpm, 97.5, 1e-9);
}
