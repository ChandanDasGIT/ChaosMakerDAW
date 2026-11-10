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
    t2.pan = -0.5f;
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
    CHECK(r1.panLeft == 1.0f && r1.panRight == 1.0f);           // centre: unchanged
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
    CHECK(r2.panLeft == 1.0f);                                   // panned left: right side down
    CHECK_NEAR(r2.panRight, std::sqrt(0.5), 1e-6);
    CHECK(r2.clips.empty());

    CHECK(snapshot->keepAudio.size() == 2);
}

TEST(snapshotbuilder, build_playback_snapshot_with_effects_and_monitoring)
{
    Project project;
    // A guitar type: its effect list holds GuitarSuite, so the pedal chain runs (plan 6.1).
    auto& t = project.addTrack(TrackKind::Audio, "Gtr", "Electric Guitar");
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
    fx.eq = std::make_shared<TenBandEq>();
    fx.playbackEq = std::make_shared<TenBandEq>();
    fx.chaosEq = std::make_shared<ChaosEq>();
    fx.playbackChaosEq = std::make_shared<ChaosEq>();
    fxMap[t1.id] = fx;
    chaoseq::Settings chaos;
    chaos.bands[4] = {true, false, false, chaoseq::Placement::Mid,
                      {eqband::Shape::HighShelf, 6000.0, -3.0, 1.0, eqband::Slope::Db24}};
    chaos.outputGainDb = 2.0;
    CHECK(project.setTrackChaosEq(t1.id, chaos));

    SnapshotBuilder::syncTrackEq(project, fxMap);

    // Both EQ instances (monitor and playback) carry the track's bands.
    for (int i = 0; i < 10; ++i) {
        CHECK_NEAR(fxMap[t1.id].eq->bandGain(i), t1.eqGainDb[i], 1e-6);
        CHECK_NEAR(fxMap[t1.id].playbackEq->bandGain(i), t1.eqGainDb[i], 1e-6);
    }
    // And both Chaos EQ instances carry its settings.
    for (const auto& instance : {fxMap[t1.id].chaosEq, fxMap[t1.id].playbackChaosEq}) {
        const auto s = instance->settings();
        CHECK(chaoseq::bandsInUse(s) == 1 && s.bands[4].placement == chaoseq::Placement::Mid);
        CHECK(s.bands[4].filter.shape == eqband::Shape::HighShelf);
        CHECK_NEAR(s.bands[4].filter.gainDb, -3.0, 1e-6);
        CHECK_NEAR(s.outputGainDb, 2.0, 1e-6);
    }
}

TEST(snapshotbuilder, track_chains_follow_the_effect_list_in_order)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& guitar = project.addTrack(TrackKind::Instrument, "Gtr", "Electric Guitar");
    auto& vocal = project.addTrack(TrackKind::Audio, "Vox", "Vocal");
    // A pattern clip on each so the playback chain is built as well.
    for (auto* track : {&guitar, &vocal}) {
        AudioClip clip; clip.poolId = "p"; clip.length = 48000;
        MidiNote n; n.note = 60; n.velocity = 100; n.startBeat = 0; n.lengthBeats = 1;
        clip.notes = { n };
        project.addAudioClip(track->id, clip);
    }
    std::unordered_map<int, SnapshotBuilder::TrackEffects> fxMap;
    for (int id : {guitar.id, vocal.id}) {
        auto& fx = fxMap[id];
        fx.input = std::make_shared<TrackInput>(); fx.playbackInput = std::make_shared<TrackInput>();
        fx.eq = std::make_shared<TenBandEq>(); fx.playbackEq = std::make_shared<TenBandEq>();
        fx.chaosEq = std::make_shared<ChaosEq>(); fx.playbackChaosEq = std::make_shared<ChaosEq>();
        fx.chain = std::make_shared<DummyProcessor>(); fx.playbackChain = std::make_shared<DummyProcessor>();
    }
    const auto build = [&] {
        return SnapshotBuilder::buildPlaybackSnapshot(project, nullptr,
            [&](int id) -> const SnapshotBuilder::TrackEffects* { return &fxMap.at(id); }, false);
    };
    const auto chains = [&](const EngineSnapshot& snap, int id) {
        const auto& t = *std::find_if(snap.tracks.begin(), snap.tracks.end(), [id](const auto& r) { return r.trackId == id; });
        return std::pair{t.monitorChain, t.chain};
    };
    using Chain = std::vector<Processor*>;
    const auto& g = fxMap.at(guitar.id);
    const auto& v = fxMap.at(vocal.id);

    // Guitar starts with GuitarSuite only; the vocal starts empty, so its EQ and pedals do not run.
    auto snap = build();
    CHECK(chains(*snap, guitar.id).first == (Chain{g.input.get(), g.chain.get()}));
    CHECK(chains(*snap, guitar.id).second == (Chain{g.playbackInput.get(), g.playbackChain.get()}));
    CHECK(chains(*snap, vocal.id).first == (Chain{v.input.get()}));
    CHECK(chains(*snap, vocal.id).second == (Chain{v.playbackInput.get()}));

    // Added EQ runs after GuitarSuite; moved first, it runs before it.
    const int eq = project.addTrackEffect(guitar.id, "eq");
    snap = build();
    CHECK(chains(*snap, guitar.id).first == (Chain{g.input.get(), g.chain.get(), g.eq.get()}));
    CHECK(project.moveTrackEffect(guitar.id, eq, 0));
    snap = build();
    CHECK(chains(*snap, guitar.id).first == (Chain{g.input.get(), g.eq.get(), g.chain.get()}));
    CHECK(chains(*snap, guitar.id).second == (Chain{g.playbackInput.get(), g.playbackEq.get(), g.playbackChain.get()}));
    CHECK(project.addTrackEffect(vocal.id, "eq") > 0);
    snap = build();
    CHECK(chains(*snap, vocal.id).first == (Chain{v.input.get(), v.eq.get()}));
    // Chaos EQ runs beside the 10-band EQ, at its place in the list.
    const int chaos = project.addTrackEffect(vocal.id, "chaoseq");
    snap = build();
    CHECK(chains(*snap, vocal.id).first == (Chain{v.input.get(), v.eq.get(), v.chaosEq.get()}));
    CHECK(chains(*snap, vocal.id).second == (Chain{v.playbackInput.get(), v.playbackEq.get(), v.playbackChaosEq.get()}));
    CHECK(project.moveTrackEffect(vocal.id, chaos, 0));
    snap = build();
    CHECK(chains(*snap, vocal.id).first == (Chain{v.input.get(), v.chaosEq.get(), v.eq.get()}));
    CHECK(project.removeTrackEffect(vocal.id, chaos));
    snap = build();
    CHECK(chains(*snap, vocal.id).first == (Chain{v.input.get(), v.eq.get()}));
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

TEST(snapshotbuilder, build_playback_snapshot_pattern_clip_notes)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);

    auto& t = project.addTrack(TrackKind::Instrument, "Synth", "Instrument");

    AudioClip clip;
    clip.poolId = "Clip 1";
    clip.start = 12000;
    clip.length = 48000;       // [12000, 60000]
    clip.sourceOffset = 12000; // origin = 0

    // Notes out of order intentionally to test sorting
    // Note 1: hidden on the left (on=0, off=6000) -> dropped
    MidiNote n1; n1.note = 60; n1.velocity = 80; n1.startBeat = 0.0; n1.lengthBeats = 0.25;

    // Note 3: normal note in the middle (on=24000, off=36000)
    MidiNote n3; n3.note = 64; n3.velocity = 100; n3.startBeat = 1.0; n3.lengthBeats = 0.5;

    // Note 2: left-trimmed (on=6000 clamped to 12000, off=18000)
    MidiNote n2; n2.note = 62; n2.velocity = 90; n2.startBeat = 0.25; n2.lengthBeats = 0.5;

    // Note 4: right-trimmed across end (on=48000, off=72000 clamped to 60000)
    MidiNote n4; n4.note = 65; n4.velocity = 110; n4.startBeat = 2.0; n4.lengthBeats = 1.0;

    // Note 5: hidden past the right (on=72000 clamped to 72000, off=84000 clamped to 60000) -> dropped
    MidiNote n5; n5.note = 67; n5.velocity = 120; n5.startBeat = 3.0; n5.lengthBeats = 0.5;

    // Note 6: starts before Note 3 ends, but ends before Note 3 ends, to test noteOffs sort order vs notes sort order
    // Let's test noteOffs:
    // n_long: on = 20000 (beat ~0.8333), off = 40000 (beat ~1.6667)
    // n_short: on = 22000 (beat ~0.9167), off = 26000 (beat ~1.0833)
    // Here we have n2 (on=12000, off=18000), n3 (on=24000, off=36000), n4 (on=48000, off=60000)
    // Let's add n_long and n_short:
    // origin is 0, so beatsToSamples:
    // beat 0.8 = 19200 samples; length = 1.0 beat (24000) -> off = 43200 samples
    // beat 0.9 = 21600 samples; length = 0.1 beat (2400) -> off = 24000 samples
    MidiNote n_long; n_long.note = 70; n_long.velocity = 100; n_long.startBeat = 0.8; n_long.lengthBeats = 1.0;
    MidiNote n_short; n_short.note = 71; n_short.velocity = 100; n_short.startBeat = 0.9; n_short.lengthBeats = 0.1;

    clip.notes = { n1, n3, n2, n4, n5, n_long, n_short };
    project.addAudioClip(t.id, clip);

    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(project, nullptr, nullptr, false);
    CHECK(snapshot != nullptr);
    CHECK(snapshot->tracks.size() == 1);
    CHECK(snapshot->playbackEnd == 60000); // where the clip ends; rings fade after the stop

    const auto& render = snapshot->tracks[0];
    // n1 and n5 should be dropped. 5 notes remain: n2, n3, n4, n_long, n_short.
    CHECK(render.notes.size() == 5);
    CHECK(render.noteOffs.size() == 5);

    // Verify render.notes is sorted by .on
    for (size_t i = 1; i < render.notes.size(); ++i) {
        CHECK(render.notes[i - 1].on <= render.notes[i].on);
    }

    // Expected on values:
    // n2: on=12000, off=18000, key=62, vel=90
    // n_long: on=19200, off=43200, key=70, vel=100
    // n_short: on=21600, off=24000, key=71, vel=100
    // n3: on=24000, off=36000, key=64, vel=100
    // n4: on=48000, off=60000, key=65, vel=110
    CHECK(render.notes[0].key == 62);
    CHECK(render.notes[0].on == 12000);
    CHECK(render.notes[0].off == 18000);
    CHECK(render.notes[0].velocity == 90);

    CHECK(render.notes[1].key == 70);
    CHECK(render.notes[1].on == 19200);
    CHECK(render.notes[1].off == 43200);

    CHECK(render.notes[2].key == 71);
    CHECK(render.notes[2].on == 21600);
    CHECK(render.notes[2].off == 24000);

    CHECK(render.notes[3].key == 64);
    CHECK(render.notes[3].on == 24000);
    CHECK(render.notes[3].off == 36000);

    CHECK(render.notes[4].key == 65);
    CHECK(render.notes[4].on == 48000);
    CHECK(render.notes[4].off == 60000);

    // Verify render.noteOffs is sorted by .off
    for (size_t i = 1; i < render.noteOffs.size(); ++i) {
        CHECK(render.noteOffs[i - 1].off <= render.noteOffs[i].off);
    }

    // Expected off order:
    // 1. n2 (off=18000)
    // 2. n_short (off=24000)
    // 3. n3 (off=36000)
    // 4. n_long (off=43200)
    // 5. n4 (off=60000)
    CHECK(render.noteOffs[0].key == 62);
    CHECK(render.noteOffs[0].off == 18000);

    CHECK(render.noteOffs[1].key == 71);
    CHECK(render.noteOffs[1].off == 24000);

    CHECK(render.noteOffs[2].key == 64);
    CHECK(render.noteOffs[2].off == 36000);

    CHECK(render.noteOffs[3].key == 70);
    CHECK(render.noteOffs[3].off == 43200);

    CHECK(render.noteOffs[4].key == 65);
    CHECK(render.noteOffs[4].off == 60000);
}

TEST(snapshotbuilder, instrument_track_chain_starts_with_instrument)
{
    Project project;
    // A guitar type, so GuitarSuite (the insert effect here) is on the track's effect list.
    auto& tInst = project.addTrack(TrackKind::Instrument, "Guitar Track", "Electric Guitar");
    auto& tAudio = project.addTrack(TrackKind::Audio, "Audio Track", "Audio");

    AudioClip patternClip;
    patternClip.poolId = "Clip 1";
    patternClip.start = 0;
    patternClip.length = 48000;
    MidiNote n;
    n.note = 60;
    n.velocity = 100;
    n.startBeat = 0.0;
    n.lengthBeats = 1.0;
    patternClip.notes = { n };
    project.addAudioClip(tInst.id, patternClip);

    auto synth = std::make_shared<DummyProcessor>();
    auto insertEffect = std::make_shared<DummyProcessor>();

    SnapshotBuilder::TrackEffects fxInst;
    fxInst.instrument = synth;
    fxInst.playbackChain = insertEffect;

    SnapshotBuilder::TrackEffects fxAudio;

    std::unordered_map<int, SnapshotBuilder::TrackEffects> effectsMap;
    effectsMap[tInst.id] = fxInst;
    effectsMap[tAudio.id] = fxAudio;

    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(project, nullptr, effectsMap, false);
    CHECK(snapshot != nullptr);
    CHECK(snapshot->tracks.size() == 2);

    const auto& rInst = snapshot->tracks[0];
    CHECK(rInst.trackId == tInst.id);
    // Instrument track chain: instrument first, then insert effect
    CHECK(rInst.chain.size() == 2);
    CHECK(rInst.chain[0] == synth.get());
    CHECK(rInst.chain[1] == insertEffect.get());

    const auto& rAudio = snapshot->tracks[1];
    CHECK(rAudio.trackId == tAudio.id);
    // Audio track has no instrument
    CHECK(rAudio.chain.empty());
}

TEST(snapshotbuilder, bpm_change_updates_sample_times_and_release_tail)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);

    auto& tInst = project.addTrack(TrackKind::Instrument, "Synth Track", "Instrument");
    AudioClip clip;
    clip.poolId = "Clip 1";
    clip.start = 0;
    clip.length = 48000;
    MidiNote n;
    n.note = 60;
    n.velocity = 100;
    n.startBeat = 1.0;
    n.lengthBeats = 1.0;
    clip.notes = { n };
    project.addAudioClip(tInst.id, clip);

    // At 120 BPM, 48kHz: beat 1.0 is sample 24000, beat 2.0 is sample 48000.
    auto snap120 = SnapshotBuilder::buildPlaybackSnapshot(project, nullptr, nullptr, false);
    CHECK(snap120 != nullptr);
    CHECK(snap120->tracks[0].notes.size() == 1);
    CHECK(snap120->tracks[0].notes[0].on == 24000);
    CHECK(snap120->tracks[0].notes[0].off == 48000);
    CHECK(snap120->playbackEnd == 48000);                       // the clip's end, no tail

    // Change tempo to 60 BPM with 96000 sample clip
    Project project60;
    project60.tempo().set(60.0, 4, 4, 48000.0);
    clip.length = 96000;
    auto& t2 = project60.addTrack(TrackKind::Instrument, "Synth", "Instrument");
    project60.addAudioClip(t2.id, clip);

    auto snap60 = SnapshotBuilder::buildPlaybackSnapshot(project60, nullptr, nullptr, false);
    CHECK(snap60 != nullptr);
    CHECK(snap60->tracks[0].notes.size() == 1);
    // At 60 BPM, 48kHz: beat 1.0 is sample 48000, beat 2.0 is sample 96000.
    CHECK(snap60->tracks[0].notes[0].on == 48000);
    CHECK(snap60->tracks[0].notes[0].off == 96000);
    CHECK(snap60->playbackEnd == 96000);
}


TEST(snapshotbuilder, playback_ends_where_the_last_clip_ends)
{
    // Every kind of clip ends playback exactly at its end: the playhead never runs past the
    // clips. What still rings (notes, effects) fades out after the stop (AudioEngine).
    const auto build = [](bool notes, bool audio, TrackKind kind) {
        Project project;
        project.tempo().set(120.0, 4, 4, 48000.0);
        auto& track = project.addTrack(kind, "Track", kind == TrackKind::Instrument ? "Electric Guitar" : "Vocal");
        AudioClip clip;
        clip.poolId = "take";
        clip.length = 96000;
        if (notes) {
            MidiNote n; n.note = 60; n.velocity = 100; n.startBeat = 0; n.lengthBeats = 1;
            clip.notes = { n };
        }
        project.addAudioClip(track.id, clip);
        auto data = makeDummyAudio(96000);
        auto resolver = [&](const std::string&) -> std::shared_ptr<const AudioData> { return audio ? data : nullptr; };
        return SnapshotBuilder::buildPlaybackSnapshot(project, resolver, nullptr, false)->playbackEnd;
    };
    CHECK(build(false, false, TrackKind::Instrument) == 96000);         // empty pattern clip
    CHECK(build(true, false, TrackKind::Instrument) == 96000);          // pattern notes
    CHECK(build(false, true, TrackKind::Instrument) == 96000);          // recorded audio through FX
    CHECK(build(false, true, TrackKind::Audio) == 96000);               // plain audio
}

TEST(snapshotbuilder, volume_envelope_reaches_the_track_render)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    project.addTrack(TrackKind::Audio, "Plain");
    auto& faded = project.addTrack(TrackKind::Audio, "Faded");
    CHECK(project.addEnvelopePoint(faded.id, 0, automation::envelopescale::unityLevel()) == 0);
    CHECK(project.addEnvelopePoint(faded.id, 4800, 0.0) == 1);

    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(project, nullptr, nullptr, false);
    CHECK(snapshot->tracks.size() == 2);
    CHECK(!snapshot->tracks[0].envelope.active());
    const auto& envelope = snapshot->tracks[1].envelope;
    CHECK(envelope.active());
    CHECK(envelope.points().size() == 2);
    CHECK_NEAR(envelope.gainAt(0), 1.0, 1e-9);
    CHECK_NEAR(envelope.gainAt(4800), 0.0, 1e-9);

    project.setEnvelopeBypassed(faded.id, true);
    auto bypassed = SnapshotBuilder::buildPlaybackSnapshot(project, nullptr, nullptr, false);
    CHECK(!bypassed->tracks[1].envelope.active());
    CHECK_NEAR(bypassed->tracks[1].envelope.gainAt(4800), 1.0, 1e-9);
}

TEST(snapshotbuilder, a_mixing_plugin_runs_at_its_place_in_the_chain_with_the_tracks_settings)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& vocal = project.addTrack(TrackKind::Audio, "Vox");
    vocal.type = "Vocal";
    CHECK(project.setTrackEffects(vocal.id, {"chaoseq", "compressor"}));
    auto settings = mixfx::defaults(mixfx::Compressor);
    settings.values["ratio"] = 10;
    CHECK(project.setTrackMixEffect(vocal.id, mixfx::Compressor, settings));

    SnapshotBuilder::TrackEffects fx;
    fx.chaosEq = std::make_shared<ChaosEq>();
    fx.playbackChaosEq = std::make_shared<ChaosEq>();
    SnapshotBuilder::ensureMixEffects(fx, {"chaoseq", "compressor"}, 48000.0, 256);
    CHECK(fx.mixEffects.size() == 1 && fx.playbackMixEffects.size() == 1);   // only what is on the chain
    const auto first = fx.mixEffects.at("compressor");
    SnapshotBuilder::ensureMixEffects(fx, {"compressor"}, 48000.0, 256);
    CHECK(fx.mixEffects.at("compressor") == first);                         // kept, not made again
    SnapshotBuilder::remakeMixEffects(fx, 48000.0, 256);
    CHECK(fx.mixEffects.size() == 1 && fx.mixEffects.at("compressor") != first);   // a new device: fresh
    std::unordered_map<int, SnapshotBuilder::TrackEffects> effects{{vocal.id, fx}};
    SnapshotBuilder::syncTrackEq(project, effects);
    CHECK(fx.mixEffects.at("compressor")->parameter("ratio")->get() == 10.0f);
    CHECK(fx.playbackMixEffects.at("compressor")->parameter("ratio")->get() == 10.0f);

    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(project, nullptr,
        [&](int id) -> const SnapshotBuilder::TrackEffects* { return id == vocal.id ? &fx : nullptr; }, false);
    const auto& chain = snapshot->tracks[0].monitorChain;
    CHECK(chain.size() == 2);
    CHECK(chain.size() == 2 && chain[0] == fx.chaosEq.get() && chain[1] == fx.mixEffects.at("compressor").get());

    // Off the list: it does not run.
    CHECK(project.setTrackEffects(vocal.id, {"chaoseq"}));
    auto without = SnapshotBuilder::buildPlaybackSnapshot(project, nullptr,
        [&](int id) -> const SnapshotBuilder::TrackEffects* { return id == vocal.id ? &fx : nullptr; }, false);
    CHECK(without->tracks[0].monitorChain.size() == 1);
}

TEST(snapshotbuilder, the_master_tracks_effects_run_on_the_mix_in_order)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    project.addTrack(TrackKind::Audio, "Vox");
    CHECK(project.setTrackEffects(Project::MasterTrackId, {"chaoseq", "limiter"}));
    auto limiter = mixfx::defaults(mixfx::Limiter);
    limiter.values["ceiling"] = -6;
    CHECK(project.setTrackMixEffect(Project::MasterTrackId, mixfx::Limiter, limiter));
    SnapshotBuilder::TrackEffects master;
    master.chaosEq = std::make_shared<ChaosEq>();
    master.playbackChaosEq = std::make_shared<ChaosEq>();
    SnapshotBuilder::ensureMixEffects(master, {"chaoseq", "limiter"}, 48000.0, 256);
    std::unordered_map<int, SnapshotBuilder::TrackEffects> effects{{Project::MasterTrackId, master}};
    SnapshotBuilder::syncTrackEq(project, effects);
    CHECK(master.playbackMixEffects.at("limiter")->parameter("ceiling")->get() == -6.0f);
    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(project, nullptr,
        [&](int id) -> const SnapshotBuilder::TrackEffects* { return id == Project::MasterTrackId ? &master : nullptr; }, false);
    CHECK(snapshot->masterEffects.size() == 2);
    CHECK(snapshot->masterEffects.size() == 2 && snapshot->masterEffects[0] == master.playbackChaosEq.get()
          && snapshot->masterEffects[1] == master.playbackMixEffects.at("limiter").get());
    CHECK(snapshot->tracks.size() == 1);   // the master is not a track of the mix
}

TEST(snapshotbuilder, a_clips_effects_become_its_chain)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& track = project.addTrack(TrackKind::Audio, "Vox");
    AudioClip clip;
    clip.poolId = "a";
    clip.length = 4800;
    const int id = project.addAudioClip(track.id, clip);
    AudioClip plain = clip;
    plain.start = 9600;
    project.addAudioClip(track.id, plain);
    CHECK(project.setTrackEffects(Project::clipEffectsId(id), {"compressor", "chaoseq"}));
    SnapshotBuilder::TrackEffects clipFx;
    clipFx.chaosEq = std::make_shared<ChaosEq>();
    clipFx.playbackChaosEq = std::make_shared<ChaosEq>();
    SnapshotBuilder::ensureMixEffects(clipFx, {"compressor", "chaoseq"}, 48000.0, 256);
    auto audio = makeDummyAudio(48000);
    auto snapshot = SnapshotBuilder::buildPlaybackSnapshot(project,
        [&](const std::string&) -> std::shared_ptr<const AudioData> { return audio; },
        [&](int fxId) -> const SnapshotBuilder::TrackEffects* { return fxId == Project::clipEffectsId(id) ? &clipFx : nullptr; }, false);
    const auto& clips = snapshot->tracks[0].clips;
    CHECK(clips.size() == 2);
    CHECK(clips.size() == 2 && clips[0].chain.size() == 2 && clips[1].chain.empty());
    CHECK(clips.size() == 2 && clips[0].chain[0] == clipFx.playbackMixEffects.at("compressor").get()
          && clips[0].chain[1] == clipFx.playbackChaosEq.get());
}
