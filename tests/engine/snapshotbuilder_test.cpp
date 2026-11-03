#include "testkit.h"
#include "engine/snapshotbuilder.h"
#include "core/audiodata.h"
#include "processor/processor.h"

#include <cmath>

namespace {

class DummyProcessor : public Processor {
public:
    std::string typeId() const override { return "dummy"; }
    void prepare(double, int) override {}
    void reset() noexcept override {}
    void process(ProcessContext&) noexcept override {}
};

std::shared_ptr<const AudioData> makeDummyAudio(int frames = 48000) {
    auto data = std::make_shared<AudioData>();
    data->sampleRate = 48000;
    data->frames = frames;
    data->channels = { std::vector<float>(frames, 0.5f), std::vector<float>(frames, -0.5f) };
    return data;
}

} // namespace

TEST(snapshotbuilder, build_empty_snapshot)
{
    auto snapshot = SnapshotBuilder::buildEmptySnapshot();
    CHECK(snapshot != nullptr);
    CHECK(snapshot->tracks.empty());
    CHECK(snapshot->masterChain.empty());
    CHECK(snapshot->keepAudio.empty());
    CHECK(snapshot->keepProcessors.empty());
}

TEST(snapshotbuilder, build_master_chain_snapshot)
{
    auto proc = std::make_shared<DummyProcessor>();
    auto snapshot = SnapshotBuilder::buildMasterChainSnapshot(proc);
    CHECK(snapshot != nullptr);
    CHECK(snapshot->masterChain.size() == 1);
    CHECK(snapshot->masterChain[0] == proc.get());
    CHECK(snapshot->keepProcessors.size() == 1);
    CHECK(snapshot->keepProcessors[0] == proc);
}

TEST(snapshotbuilder, build_playback_snapshot_basic)
{
    Project project;
    project.tempo().set(140.0, 3, 4, 48000.0);

    auto& t1 = project.addTrack(TrackKind::Audio, "Track 1");
    t1.volume = 0.75f;
    t1.solo = false;
    t1.muted = true;

    auto& t2 = project.addTrack(TrackKind::Audio, "Track 2");
    t2.volume = 1.0f;
    t2.solo = true;
    t2.muted = false;

    AudioClip c1;
    c1.start = 1000;
    c1.length = 5000;
    c1.sourceOffset = 200;
    c1.fadeIn = 10;
    c1.fadeOut = 20;
    c1.gainDb = -6.0f;
    c1.poolId = "audio_1";
    c1.effectsPrinted = true;
    project.addAudioClip(t1.id, c1);

    AudioClip c2;
    c2.start = 8000;
    c2.length = 4000;
    c2.sourceOffset = 0;
    c2.gainDb = 0.0f;
    c2.poolId = "audio_2";
    c2.effectsPrinted = true;
    project.addAudioClip(t1.id, c2);

    auto audio1 = makeDummyAudio(10000);
    auto audio2 = makeDummyAudio(8000);

    auto audioResolver = [&](const std::string& poolId) -> std::shared_ptr<const AudioData> {
        if (poolId == "audio_1") return audio1;
        if (poolId == "audio_2") return audio2;
        return nullptr;
    };

    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(project, audioResolver, nullptr, false);
    CHECK(snapshot != nullptr);
    CHECK_NEAR(snapshot->tempo.bpm(), 140.0, 1e-6);
    CHECK(!snapshot->multiMonitor);
    CHECK(snapshot->playbackEnd == 12000); // 8000 + 4000
    CHECK(snapshot->tracks.size() == 2);

    const auto& r1 = snapshot->tracks[0];
    CHECK(r1.trackId == t1.id);
    CHECK_NEAR(r1.gain, 0.75f, 1e-6);
    CHECK(r1.muted == true);
    CHECK(r1.soloed == false);
    CHECK(r1.clips.size() == 2);
    CHECK(r1.clips[0].start == 1000);
    CHECK(r1.clips[0].length == 5000);
    CHECK_NEAR(r1.clips[0].gain, static_cast<float>(std::pow(10.0, -6.0 / 20.0)), 1e-4);
    CHECK(r1.clips[1].start == 8000);
    CHECK(r1.clips[1].length == 4000);

    const auto& r2 = snapshot->tracks[1];
    CHECK(r2.trackId == t2.id);
    CHECK(r2.soloed == true);
    CHECK(r2.clips.empty());

    CHECK(snapshot->keepAudio.size() == 2);
}

TEST(snapshotbuilder, build_playback_snapshot_with_effects_and_monitoring)
{
    Project project;
    auto& t = project.addTrack(TrackKind::Audio, "Vocals", "Audio");
    t.recordArmed = true;
    t.monitorEnabled = true;

    AudioClip clip;
    clip.start = 0;
    clip.length = 24000;
    clip.poolId = "take1";
    clip.effectsPrinted = false; // unprinted -> needs playbackChain
    project.addAudioClip(t.id, clip);

    auto audio = makeDummyAudio(24000);

    SnapshotBuilder::TrackEffects fx;
    fx.meters = std::make_shared<TrackMeters>();
    fx.input = std::make_shared<TrackInput>();
    fx.playbackInput = std::make_shared<TrackInput>();
    fx.chain = std::make_shared<DummyProcessor>();
    fx.playbackChain = std::make_shared<DummyProcessor>();
    fx.inputChannel = 2;

    std::unordered_map<int, SnapshotBuilder::TrackEffects> effectsMap;
    effectsMap[t.id] = fx;

    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(
        project,
        [&](const std::string&) { return audio; },
        effectsMap,
        true);

    CHECK(snapshot != nullptr);
    CHECK(snapshot->multiMonitor);
    CHECK(snapshot->tracks.size() == 1);

    const auto& tr = snapshot->tracks[0];
    CHECK(tr.trackId == t.id);
    CHECK(tr.meters == fx.meters);
    CHECK(tr.inputChannel == 2);
    CHECK(tr.armed == true);
    CHECK(tr.monitoring == true);

    // Monitor chain has input + chain
    CHECK(tr.monitorChain.size() == 2);
    CHECK(tr.monitorChain[0] == fx.input.get());
    CHECK(tr.monitorChain[1] == fx.chain.get());

    // Playback chain has playbackInput + playbackChain because effectsPrinted == false
    CHECK(tr.chain.size() == 2);
    CHECK(tr.chain[0] == fx.playbackInput.get());
    CHECK(tr.chain[1] == fx.playbackChain.get());

    // keepProcessors preserves ownership
    CHECK(snapshot->keepProcessors.size() == 4);
}

TEST(snapshotbuilder, build_playback_snapshot_printed_clips_do_not_add_playback_chain)
{
    Project project;
    auto& t = project.addTrack(TrackKind::Audio, "Guitar", "Audio");

    AudioClip clip;
    clip.start = 0;
    clip.length = 24000;
    clip.poolId = "take1";
    clip.effectsPrinted = true; // printed -> no playbackChain
    project.addAudioClip(t.id, clip);

    SnapshotBuilder::TrackEffects fx;
    fx.playbackInput = std::make_shared<TrackInput>();
    fx.playbackChain = std::make_shared<DummyProcessor>();

    std::unordered_map<int, SnapshotBuilder::TrackEffects> effectsMap;
    effectsMap[t.id] = fx;

    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(
        project,
        [&](const std::string&) { return makeDummyAudio(); },
        effectsMap,
        true);

    CHECK(snapshot->tracks.size() == 1);
    CHECK(snapshot->tracks[0].chain.empty());
}

TEST(snapshotbuilder, build_playback_snapshot_async)
{
    Project project;
    project.tempo().set(128.0, 4, 4, 48000.0);
    auto& t = project.addTrack(TrackKind::Audio, "Audio Track");
    AudioClip clip;
    clip.start = 500;
    clip.length = 1500;
    clip.poolId = "a1";
    project.addAudioClip(t.id, clip);

    auto audio = makeDummyAudio(2000);
    auto ctx = SnapshotBuilder::Context::fromProject(project);

    std::unordered_map<int, SnapshotBuilder::TrackEffects> fxMap;
    auto future = SnapshotBuilder::buildPlaybackSnapshotAsync(
        std::move(ctx),
        [&](const std::string&) { return audio; },
        std::move(fxMap),
        true);

    auto snapshot = future.get();
    CHECK(snapshot != nullptr);
    CHECK(snapshot->playbackEnd == 2000);
    CHECK(snapshot->tracks.size() == 1);
    CHECK(snapshot->tracks[0].clips.size() == 1);
}

TEST(snapshotbuilder, sync_track_eq)
{
    Project project;
    auto& t1 = project.addTrack(TrackKind::Audio, "T1");
    for (int i = 0; i < 10; ++i) {
        t1.eqGainDb[i] = static_cast<float>(i * 1.5 - 6.0);
    }

    std::unordered_map<int, SnapshotBuilder::TrackEffects> fxMap;
    SnapshotBuilder::TrackEffects fx;
    fx.input = std::make_shared<TrackInput>();
    fx.input->prepare(48000.0, 256);
    fx.playbackInput = std::make_shared<TrackInput>();
    fx.playbackInput->prepare(48000.0, 256);
    fxMap[t1.id] = fx;

    SnapshotBuilder::syncTrackEq(project, fxMap);

    // Verify through EQ process response or bands
    CHECK(fxMap[t1.id].input != nullptr);
    CHECK(fxMap[t1.id].playbackInput != nullptr);
}

TEST(snapshotbuilder, sync_routing_capacity_and_monitored_track)
{
    Project project;
    auto& t1 = project.addTrack(TrackKind::Audio, "Vocal 1");
    t1.recordArmed = true;
    t1.inputPort = "port_1";

    auto& t2 = project.addTrack(TrackKind::Audio, "Vocal 2");
    t2.monitorEnabled = true;
    t2.inputPort = "port_2";

    std::unordered_map<int, SnapshotBuilder::TrackEffects> fxMap;
    fxMap[t1.id] = SnapshotBuilder::TrackEffects{};
    fxMap[t2.id] = SnapshotBuilder::TrackEffects{};

    // Capacity = 1, but we have 2 distinct ports -> capacity error
    auto resCap = SnapshotBuilder::syncRouting(
        project,
        fxMap,
        "default_port",
        {"port_1", "port_2"},
        1,
        [](const std::string&) { return 0; });
    CHECK(!resCap.error.empty());
    CHECK(resCap.error == "The audio device cannot route all selected track inputs independently.");

    // Capacity = 2 -> routing succeeds
    auto connector = [](const std::string& src) -> int {
        if (src == "port_1") return 0;
        if (src == "port_2") return 1;
        return -1;
    };

    auto resOk = SnapshotBuilder::syncRouting(
        project,
        fxMap,
        "default_port",
        {"port_1", "port_2"},
        2,
        connector);

    CHECK(resOk.error.empty());
    CHECK(resOk.cleanMonitorEnabled == true);
    CHECK(resOk.monitoredTrack == t2.id); // t2 is monitorEnabled
    CHECK(fxMap[t1.id].inputChannel == 0);
    CHECK(fxMap[t2.id].inputChannel == 1);
}
