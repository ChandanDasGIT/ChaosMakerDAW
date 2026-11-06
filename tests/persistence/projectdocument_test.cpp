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
    t1.pan = -0.35f;
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
    CHECK_NEAR(track->pan, -0.35f, 1e-6);
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

namespace {
std::string sessionWithClip(const std::string& clipExtra)
{
    return R"({"format":"ChaosMakerProject","version":2,"project":{"version":1,"bpm":120.0,
        "sampleRate":48000.0,"numerator":4,"denominator":4,"tracks":[{"name":"Keys","type":"Piano",
        "instrument":true,"volume":1.0,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[{"path":"Clip 1",
        "hasAudio":false,"start":"0","length":"96000","offset":"24000")" + clipExtra + R"(}]}]},
        "trackEffects":[""]})";
}
}

TEST(projectdocument, clip_notes_round_trip_and_stay_clip_relative)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& t = project.addTrack(TrackKind::Instrument, "Keys", "Piano");
    AudioClip clip;
    clip.poolId = "Clip 1";
    // 120 bpm @ 48 kHz: 1 beat = 24000 samples
    clip.start = 48000;          // beat 2
    clip.length = 96000;         // 4 beats
    clip.sourceOffset = 12000;   // left-trimmed by 0.5 beat
    MidiNote a; a.note = 60; a.startBeat = 1.0; a.lengthBeats = 0.25; a.velocity = 90; a.selected = true;
    MidiNote hidden; hidden.note = 72; hidden.startBeat = 0.25; hidden.lengthBeats = 0.5; hidden.velocity = 30;
    clip.notes = {a, hidden};
    project.addAudioClip(t.id, std::move(clip));

    const std::string jsonStr = ProjectDocument::serialize(project, {}, {""});
    // Clip-relative in the file: 1.0 - 0.5 offset = 0.5; the trimmed-off note is kept at -0.25
    CHECK(jsonStr.find("\"start\":0.5") != std::string::npos);
    CHECK(jsonStr.find("\"start\":-0.25") != std::string::npos);
    CHECK(jsonStr.find("selected") == std::string::npos);

    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(jsonStr, loaded, error, 48000.0));
    const auto& notes = loaded.project.tracks()[0]->audioClips[0].notes;
    CHECK(notes.size() == 2);
    CHECK(notes[0].note == 60 && notes[0].velocity == 90 && !notes[0].selected);
    CHECK_NEAR(notes[0].startBeat, 1.0, 1e-9);
    CHECK_NEAR(notes[0].lengthBeats, 0.25, 1e-9);
    CHECK(notes[1].note == 72);
    CHECK_NEAR(notes[1].startBeat, 0.25, 1e-9);
    CHECK(notes[0].id != notes[1].id);

    // Reopened at another rate: the offset isn't rescaled, but clip-relative times are kept
    ProjectFileContent at96k;
    CHECK(ProjectDocument::deserialize(jsonStr, at96k, error, 96000.0));
    const auto& c96 = at96k.project.tracks()[0]->audioClips[0];
    const double offset96 = double(c96.sourceOffset) * 120.0 / (60.0 * 96000.0);
    CHECK_NEAR(c96.notes[0].startBeat - offset96, 0.5, 1e-9);
}

TEST(projectdocument, clip_notes_are_optional_and_validated)
{
    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(sessionWithClip(""), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->audioClips[0].notes.empty());

    CHECK(ProjectDocument::deserialize(sessionWithClip(
        R"(,"notes":[{"pitch":64,"start":0.0,"length":1.0,"velocity":100}])"), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->audioClips[0].notes.size() == 1);

    for (const char* bad : {R"(,"notes":{})", R"(,"notes":[{"pitch":128,"start":0,"length":1,"velocity":1}])",
                            R"(,"notes":[{"pitch":60,"start":0,"length":0,"velocity":1}])",
                            R"(,"notes":[{"pitch":60,"start":0,"length":1,"velocity":200}])",
                            R"(,"notes":[{"pitch":"C4","start":0,"length":1,"velocity":1}])",
                            R"(,"notes":[{"pitch":60,"length":1,"velocity":1}])"}) {
        error.clear();
        CHECK(!ProjectDocument::deserialize(sessionWithClip(bad), loaded, error, 48000.0));
        CHECK(error == "Invalid clip notes.");
    }
}

TEST(projectdocument, track_pan_is_optional_and_validated)
{
    const auto document = [](const std::string& panField) {
        return R"({"format":"ChaosMakerProject","version":2,"project":{"version":1,"bpm":120.0,
            "sampleRate":48000.0,"numerator":4,"denominator":4,"tracks":[{"name":"T1","type":"Audio",
            "volume":1.0)" + panField + R"(,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]}]},"trackEffects":[""]})";
    };
    ProjectFileContent loaded;
    std::string error;
    // Saved before pan existed: loads centred.
    CHECK(ProjectDocument::deserialize(document(""), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->pan == 0.0f);
    CHECK(ProjectDocument::deserialize(document(R"(,"pan":1.0)"), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->pan == 1.0f);
    CHECK(!ProjectDocument::deserialize(document(R"(,"pan":1.5)"), loaded, error, 48000.0));
    CHECK(!ProjectDocument::deserialize(document(R"(,"pan":"left")"), loaded, error, 48000.0));
}
