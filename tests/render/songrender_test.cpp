#include "testkit.h"
#include "render/songrender.h"
#include "engine/snapshotbuilder.h"
#include "core/audiodata.h"

#include <cmath>

// The offline song render for export (restMap F2).
namespace {
std::shared_ptr<const AudioData> constant(int frames, float left, float right)
{
    auto data = std::make_shared<AudioData>();
    data->sampleRate = 48000;
    data->frames = frames;
    data->channels = {std::vector<float>(std::size_t(frames), left), std::vector<float>(std::size_t(frames), right)};
    return data;
}

// One audio track with a clip of a steady level at samples [1000, 6000).
std::unique_ptr<EngineSnapshot> song(Project& project, std::shared_ptr<const AudioData>& audio)
{
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& track = project.addTrack(TrackKind::Audio, "Track 1");
    AudioClip clip;
    clip.start = 1000;
    clip.length = 5000;
    clip.poolId = "a";
    clip.effectsPrinted = true;
    project.addAudioClip(track.id, clip);
    audio = constant(5000, 0.5f, -0.5f);
    return SnapshotBuilder::buildPlaybackSnapshot(
        project, [audio](const std::string& id) { return id == "a" ? audio : nullptr; }, nullptr, true);
}
}

TEST(songrender, renders_the_range_and_the_tail_to_the_sample)
{
    Project project;
    std::shared_ptr<const AudioData> audio;
    songrender::Options o;
    o.sampleRate = 48000;
    o.start = 0;
    o.length = 6000;
    o.tail = 2000;   // after the song: silence here (nothing rings), but rendered
    const auto out = songrender::render(song(project, audio), o);
    CHECK(out.size() == 2 && out[0].size() == 8000 && out[1].size() == 8000);
    CHECK(std::abs(out[0][500]) < 1e-6f && std::abs(out[0][999]) < 1e-6f);   // before the clip
    const float level = out[0][3000];
    CHECK(level > 0.2f && level <= 0.5f + 1e-4f);                            // the clip, through the mixer
    CHECK(std::abs(out[1][3000] + level) < 1e-4f);                           // the right channel mirrors it
    CHECK(std::abs(out[0][1100] - level) < 1e-4f && std::abs(out[0][5900] - level) < 1e-4f);
    CHECK(std::abs(out[0][7000]) < 1e-6f);                                    // the tail after the clip

    // A later start: the first sample is the song at `start`.
    Project again;
    std::shared_ptr<const AudioData> audio2;
    o.start = 2000;
    o.length = 1000;
    o.tail = 0;
    const auto part = songrender::render(song(again, audio2), o);
    CHECK(part[0].size() == 1000);
    CHECK(std::abs(part[0][0] - level) < 1e-4f && std::abs(part[0][999] - level) < 1e-4f);
}

TEST(songrender, the_master_volume_and_mute_apply_and_cancel_stops)
{
    Project a;
    std::shared_ptr<const AudioData> audioA;
    songrender::Options o;
    o.sampleRate = 48000;
    o.length = 6000;
    const float full = songrender::render(song(a, audioA), o)[0][3000];

    Project b;
    std::shared_ptr<const AudioData> audioB;
    o.masterVolume = 0.5f;
    CHECK(std::abs(songrender::render(song(b, audioB), o)[0][3000] - 0.5f * full) < 1e-3f);

    Project c;
    std::shared_ptr<const AudioData> audioC;
    o.masterVolume = 1.0f;
    o.masterMuted = true;
    CHECK(std::abs(songrender::render(song(c, audioC), o)[0][3000]) < 1e-6f);

    Project d;
    std::shared_ptr<const AudioData> audioD;
    o.masterMuted = false;
    int calls = 0;
    const auto cancelled = songrender::render(song(d, audioD), o, [&](double) { return ++calls < 1; });
    CHECK(cancelled.size() == 2 && cancelled[0].empty() && cancelled[1].empty());
}

TEST(songrender, normalise_sets_the_peak_and_leaves_silence)
{
    std::vector<std::vector<float>> x{{0.25f, -0.5f}, {0.1f, 0.0f}};
    songrender::normalise(x, 0.0f);
    CHECK(std::abs(x[0][1] + 1.0f) < 1e-6f && std::abs(x[0][0] - 0.5f) < 1e-6f && std::abs(x[1][0] - 0.2f) < 1e-6f);
    songrender::normalise(x, -1.0f);
    CHECK(std::abs(std::abs(x[0][1]) - std::pow(10.0f, -1.0f / 20.0f)) < 1e-5f);
    std::vector<std::vector<float>> quiet{{0.0f, 0.0f}};
    songrender::normalise(quiet, 0.0f);
    CHECK(quiet[0][0] == 0.0f);
}

// Delay compensation (restMap G13): a track or clip through noise reduction (2048 samples of
// delay; nothing learned, so it only delays) exports exactly like one without it.
TEST(songrender, effects_with_a_delay_are_compensated_so_the_export_stays_in_time)
{
    const auto render = [](bool trackFx, bool clipFx) {
        Project project;
        project.tempo().set(120.0, 4, 4, 48000.0);
        auto& track = project.addTrack(TrackKind::Audio, "Vox");
        AudioClip clip;
        clip.start = 6000;
        clip.length = 5000;
        clip.poolId = "a";
        clip.effectsPrinted = false;
        const int id = project.addAudioClip(track.id, clip);
        if (trackFx) CHECK(project.setTrackEffects(track.id, {"denoise"}));
        if (clipFx) CHECK(project.setTrackEffects(Project::clipEffectsId(id), {"denoise"}));
        std::unordered_map<int, SnapshotBuilder::TrackEffects> effects;
        if (trackFx) SnapshotBuilder::ensureMixEffects(effects[track.id], {"denoise"}, 48000.0, 512);
        if (clipFx) SnapshotBuilder::ensureMixEffects(effects[Project::clipEffectsId(id)], {"denoise"}, 48000.0, 512);
        auto audio = constant(5000, 0.5f, -0.5f);
        songrender::Options o;
        o.sampleRate = 48000;
        o.length = 12000;
        o.tail = 6000;
        return songrender::render(SnapshotBuilder::buildPlaybackSnapshot(project,
            [audio](const std::string& p) { return p == "a" ? audio : nullptr; },
            [&](int fxId) -> const SnapshotBuilder::TrackEffects* {
                const auto it = effects.find(fxId);
                return it == effects.end() ? nullptr : &it->second;
            }, true), o);
    };
    const auto plain = render(false, false);
    CHECK(std::abs(plain[0][7000]) > 0.2f);
    for (const auto& [trackFx, clipFx] : {std::pair{true, false}, std::pair{false, true}, std::pair{true, true}}) {
        const auto out = render(trackFx, clipFx);
        double worst = 0.0;
        for (std::size_t i = 0; i < plain[0].size(); ++i) worst = std::max(worst, double(std::abs(out[0][i] - plain[0][i])));
        CHECK(worst < 1e-3);   // the clip end included: the clip effect's delayed tail is heard
    }
}
