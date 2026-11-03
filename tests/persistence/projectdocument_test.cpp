#include "testkit.h"
#include "persistence/projectdocument.h"
#include <filesystem>
#include <fstream>

using persistence::ProjectDocument;
using persistence::ProjectFileContent;

TEST(projectdocument, round_trip_serialization_and_deserialization)
{
    Project project;
    project.tempo().set(135.0, 3, 4, 48000.0);

    auto& t1 = project.addTrack(TrackKind::Audio, "Rhythm", "Audio");
    t1.volume = 0.8f;
    t1.solo = true;
    t1.muted = false;
    t1.recordDry = true;
    t1.inputPort = "system:capture_1";
    project.setTrackColour(t1.id, "#3f72af");
    for (int i = 0; i < 10; ++i) t1.eqGainDb[i] = static_cast<float>(i - 5);

    AudioClip clip;
    clip.start = 96000;
    clip.length = 48000;
    clip.sourceOffset = 24000;
    clip.gainDb = -3.5f;
    clip.effectsPrinted = false;
    clip.poolId = "take_42.wav";
    project.addAudioClip(t1.id, std::move(clip));

    TrackList::MediaPaths mediaPaths{"take_42.wav"};
    std::vector<std::string> effectsJson{R"({"pedals":[{"type":"pedal.tremolo"}]})"};

    const std::string jsonStr = ProjectDocument::serialize(project, mediaPaths, effectsJson);
    CHECK(!jsonStr.empty());

    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(jsonStr, loaded, error, 48000.0));
    CHECK(error.empty());

    CHECK_NEAR(loaded.project.tempo().bpm(), 135.0, 1e-9);
    CHECK(loaded.project.tempo().numerator() == 3);
    CHECK(loaded.project.tempo().denominator() == 4);
    CHECK(loaded.project.tracks().size() == 1);

    const auto* track = loaded.project.tracks()[0].get();
    CHECK(track->name == "Rhythm");
    CHECK(track->colour == "#3f72af");
    CHECK(track->solo == true);
    CHECK(track->muted == false);
    CHECK(track->recordDry == true);
    CHECK_NEAR(track->volume, 0.8f, 1e-6);
    CHECK(track->inputPort == "system:capture_1");
    for (int i = 0; i < 10; ++i) {
        CHECK_NEAR(track->eqGainDb[i], static_cast<float>(i - 5), 1e-6);
    }

    CHECK(track->audioClips.size() == 1);
    const auto& loadedClip = track->audioClips[0];
    CHECK(loadedClip.start == 96000);
    CHECK(loadedClip.length == 48000);
    CHECK(loadedClip.sourceOffset == 24000);
    CHECK_NEAR(loadedClip.gainDb, -3.5f, 1e-6);
    CHECK(loadedClip.effectsPrinted == false);

    CHECK(loaded.trackEffectsJson.size() == 1);
    CHECK(loaded.trackEffectsJson[0].find("pedal.tremolo") != std::string::npos);
}

TEST(projectdocument, atomic_file_save_and_load)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    project.addTrack(TrackKind::Audio, "Vocal", "Audio");

    const auto tmpPath = std::filesystem::temp_directory_path() / "test_project_atomic.chaos";
    std::string error;

    CHECK(ProjectDocument::saveToFile(tmpPath, project, {}, {"{}"}, ProjectDocument::projectFormat, &error));
    CHECK(std::filesystem::exists(tmpPath));

    ProjectFileContent loaded;
    CHECK(ProjectDocument::loadFromFile(tmpPath, loaded, error, 48000.0));
    CHECK(loaded.project.tracks().size() == 1);
    CHECK(loaded.project.tracks()[0]->name == "Vocal");

    std::error_code ec;
    std::filesystem::remove(tmpPath, ec);
}

TEST(projectdocument, schema_validation_and_rejections)
{
    ProjectFileContent loaded;
    std::string error;

    // Bad JSON
    CHECK(!ProjectDocument::deserialize("{ broken json ...", loaded, error));

    // Unknown format
    CHECK(!ProjectDocument::deserialize(R"({"format":"UnknownFormat","version":2})", loaded, error));

    // Unsupported version
    CHECK(!ProjectDocument::deserialize(R"({"format":"ChaosMakerProject","version":99})", loaded, error));

    // Mismatched trackEffects size
    const std::string mismatchJson = R"({
        "format": "ChaosMakerProject",
        "version": 2,
        "project": {
            "version": 1,
            "bpm": 120.0,
            "sampleRate": 48000.0,
            "numerator": 4,
            "denominator": 4,
            "tracks": [{"name":"T1","type":"Audio","volume":1.0,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]}]
        },
        "trackEffects": []
    })";
    CHECK(!ProjectDocument::deserialize(mismatchJson, loaded, error));

    // Invalid bpm
    const std::string badBpmJson = R"({
        "format": "ChaosMakerProject",
        "version": 2,
        "project": {
            "version": 1,
            "bpm": -50.0,
            "sampleRate": 48000.0,
            "numerator": 4,
            "denominator": 4,
            "tracks": []
        },
        "trackEffects": []
    })";
    CHECK(!ProjectDocument::deserialize(badBpmJson, loaded, error));
}

TEST(projectdocument, legacy_v1_compatibility)
{
    const std::string v1Json = R"({
        "format": "ChaosMakerSession",
        "version": 1,
        "guitarSuite": {"inputTrimDb": -2.0, "pedals": []},
        "project": {
            "version": 1,
            "bpm": 100.0,
            "sampleRate": 48000.0,
            "numerator": 4,
            "denominator": 4,
            "tracks": [
                {"name":"Track 1","type":"Audio","volume":1.0,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]},
                {"name":"Track 2","type":"Audio","volume":1.0,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]}
            ]
        }
    })";

    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(v1Json, loaded, error, 48000.0));
    CHECK(loaded.project.tracks().size() == 2);
    CHECK(loaded.trackEffectsJson.size() == 2);
    CHECK(loaded.trackEffectsJson[0].find("-2.0") != std::string::npos);
    CHECK(loaded.trackEffectsJson[1].find("-2.0") != std::string::npos);
}
