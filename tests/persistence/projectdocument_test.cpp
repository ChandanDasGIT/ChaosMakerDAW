#include "testkit.h"
#include <nlohmann/json.hpp>
#include "persistence/projectdocument.h"
#include <filesystem>
#include <fstream>

using persistence::ProjectDocument;
using persistence::ProjectFileContent;

TEST(projectdocument, round_trip_serialization_and_deserialization)
{
    Project project;
    project.tempo().set(135.0, 3, 4, 48000.0);

    // A guitar type, so its pedalboard (GuitarSuite) is kept on load (plan 6.1).
    auto& t1 = project.addTrack(TrackKind::Audio, "Rhythm", "Electric Guitar");
    t1.volume = 0.8f;
    t1.pan = -0.35f;
    t1.stripWidth = 150.0f;
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
    CHECK_NEAR(track->stripWidth, 150.0f, 1e-6);
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
                {"name":"Track 1","type":"Electric Guitar","volume":1.0,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]},
                {"name":"Track 2","type":"Audio","volume":1.0,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]}
            ]
        }
    })";

    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(v1Json, loaded, error, 48000.0));
    CHECK(loaded.project.tracks().size() == 2);
    CHECK(loaded.trackEffectsJson.size() == 2);
    // The shared v1 pedalboard loads onto the guitar; the Audio track has no GuitarSuite (plan 6.1).
    CHECK(loaded.trackEffectsJson[0].find("-2.0") != std::string::npos);
    CHECK(loaded.trackEffectsJson[1].empty());
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

    // Reopened at another rate: the trim (offset) keeps its length in beats, and the notes
    // stay where they were in the song
    ProjectFileContent at96k;
    CHECK(ProjectDocument::deserialize(jsonStr, at96k, error, 96000.0));
    const auto& c96 = at96k.project.tracks()[0]->audioClips[0];
    CHECK(c96.sourceOffset == 24000);                          // 0.5 beat at 96 kHz
    CHECK(c96.start == 96000);
    const double offset96 = double(c96.sourceOffset) * 120.0 / (60.0 * 96000.0);
    CHECK_NEAR(offset96, 0.5, 1e-9);
    CHECK_NEAR(c96.notes[0].startBeat, 1.0, 1e-9);
    CHECK_NEAR(c96.notes[0].startBeat - offset96, 0.5, 1e-9);
}

TEST(projectdocument, reopening_at_another_rate_rescales_only_pattern_clip_offsets)
{
    // 44.1 kHz file: a trimmed pattern clip and an audio clip that starts 1000 samples into its file
    const std::string doc = R"({"format":"ChaosMakerProject","version":2,"project":{"version":1,"bpm":120.0,
        "sampleRate":44100.0,"numerator":4,"denominator":4,"tracks":[{"name":"Keys","type":"Piano",
        "instrument":true,"volume":1.0,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[
          {"path":"Clip 1","hasAudio":false,"start":"44100","length":"88200","offset":"11025"},
          {"path":"take.wav","hasAudio":true,"start":"0","length":"44100","offset":"1000"}]}]},
        "trackEffects":[""]})";
    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(doc, loaded, error, 48000.0));
    const auto& clips = loaded.project.tracks()[0]->audioClips;
    CHECK(clips.size() == 2);
    CHECK(clips[0].start == 48000 && clips[0].length == 96000);
    CHECK(clips[0].sourceOffset == 12000);                     // 0.25 s = 0.5 beat, as at 44.1 kHz
    CHECK(clips[1].sourceOffset == 1000);                      // file samples: unchanged
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

TEST(projectdocument, mixer_strip_width_is_optional_and_validated)
{
    const auto document = [](const std::string& field) {
        return R"({"format":"ChaosMakerProject","version":2,"project":{"version":1,"bpm":120.0,
            "sampleRate":48000.0,"numerator":4,"denominator":4,"tracks":[{"name":"T1","type":"Audio",
            "volume":1.0)" + field + R"(,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]}]},"trackEffects":[""]})";
    };
    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(document(""), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->stripWidth == 108.0f);        // older files: default width
    CHECK(ProjectDocument::deserialize(document(R"(,"stripWidth":200)"), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->stripWidth == 200.0f);
    CHECK(!ProjectDocument::deserialize(document(R"(,"stripWidth":20)"), loaded, error, 48000.0));
    CHECK(!ProjectDocument::deserialize(document(R"(,"stripWidth":"wide")"), loaded, error, 48000.0));
}

TEST(projectdocument, track_effects_round_trip_old_files_get_defaults_and_bad_lists_are_rejected)
{
    // Saved and loaded in chain order; pedalboards only survive on tracks with GuitarSuite.
    Project project;
    auto& guitar = project.addTrack(TrackKind::Instrument, "Gtr", "Electric Guitar");
    auto& vocal = project.addTrack(TrackKind::Audio, "Vox", "Vocal");
    auto& audio = project.addTrack(TrackKind::Audio, "Aux", "Audio");
    const int eq = project.addTrackEffect(guitar.id, "eq");
    CHECK(project.moveTrackEffect(guitar.id, eq, 0));
    CHECK(project.addTrackEffect(vocal.id, "eq") > 0);
    CHECK(audio.effects.empty());
    const std::string pedals = R"({"pedals":[{"type":"pedal.tremolo"}]})";
    const auto json = ProjectDocument::serialize(project, {}, {pedals, pedals, pedals});
    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(json, loaded, error, 48000.0));
    const auto types = [&](std::size_t row) {
        std::vector<std::string> result;
        for (const auto& slot : loaded.project.tracks()[row]->effects) result.push_back(slot.type);
        return result;
    };
    using List = std::vector<std::string>;
    CHECK(types(0) == (List{"eq", "guitarsuite"}));
    CHECK(types(1) == List{"eq"});
    CHECK(types(2).empty());
    CHECK(loaded.trackEffectsJson[0].find("pedal.tremolo") != std::string::npos);
    CHECK(loaded.trackEffectsJson[1].empty());   // vocal: no GuitarSuite, pedals dropped
    CHECK(loaded.trackEffectsJson[2].empty());   // old-style pedals on an Audio track: dropped

    const auto document = [](const std::string& type, const std::string& effectsField) {
        return R"({"format":"ChaosMakerProject","version":2,"project":{"version":1,"bpm":120.0,
            "sampleRate":48000.0,"numerator":4,"denominator":4,"tracks":[{"name":"T1","type":")" + type
            + R"(","volume":1.0)" + effectsField + R"(,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]}]},"trackEffects":[""]})";
    };
    // Files saved before plan 6.1 have no list: each track gets its type's defaults (no EQ).
    CHECK(ProjectDocument::deserialize(document("Electric Guitar", ""), loaded, error, 48000.0));
    CHECK(types(0) == List{"guitarsuite"});
    CHECK(ProjectDocument::deserialize(document("Vocal", ""), loaded, error, 48000.0));
    CHECK(types(0).empty());
    // GuitarSuite is fixed on guitars: a list without it gets it back in front.
    CHECK(ProjectDocument::deserialize(document("Acoustic Guitar", R"(,"effects":["eq"])"), loaded, error, 48000.0));
    CHECK(types(0) == (List{"guitarsuite", "eq"}));
    // Broken lists are rejected like other invalid track data.
    for (const auto& [type, field] : std::vector<std::pair<std::string, std::string>>{
             {"Vocal", R"(,"effects":["guitarsuite"])"},   // only guitars have GuitarSuite
             {"Vocal", R"(,"effects":["eq","eq"])"},       // each effect at most once
             {"Vocal", R"(,"effects":["flanger"])"},       // unknown effect
             {"Vocal", R"(,"effects":"eq")"},              // not a list
             {"Vocal", R"(,"effects":[1])"}}) {            // not a type name
        CHECK(!ProjectDocument::deserialize(document(type, field), loaded, error, 48000.0));
        CHECK(error == "Invalid track effects.");
    }
}

TEST(projectdocument, volume_envelope_round_trips_is_optional_and_rescales_with_the_rate)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& track = project.addTrack(TrackKind::Audio, "Vox");
    CHECK(project.addEnvelopePoint(track.id, 0, 0.25) == 0);
    CHECK(project.addEnvelopePoint(track.id, 9'000'000'000LL, 1.0) == 1);   // past 32 bits
    CHECK(project.setEnvelopePointShape(track.id, 0, automation::CurveShape::FastEnd));
    project.setEnvelopeBypassed(track.id, true);
    project.setEnvelopeShown(track.id, true);
    project.setEnvelopeOverClips(track.id, true);
    const std::string json = ProjectDocument::serialize(project, {}, {""});

    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(json, loaded, error, 48000.0));
    const auto& envelope = loaded.project.tracks()[0]->volumeEnvelope;
    CHECK(envelope.points().size() == 2);
    CHECK(envelope.points()[0] == (automation::EnvelopePoint{0, 0.25, automation::CurveShape::FastEnd}));
    CHECK(envelope.points()[1] == (automation::EnvelopePoint{9'000'000'000LL, 1.0}));
    CHECK(envelope.bypassed() && envelope.shown() && envelope.overClips());
    CHECK(!loaded.project.canUndo());

    CHECK(ProjectDocument::deserialize(json, loaded, error, 96000.0));
    CHECK(loaded.project.tracks()[0]->volumeEnvelope.points()[1].position == 18'000'000'000LL);

    const auto document = [](const std::string& field) {
        return R"({"format":"ChaosMakerProject","version":2,"project":{"version":1,"bpm":120.0,
            "sampleRate":48000.0,"numerator":4,"denominator":4,"tracks":[{"name":"T1","type":"Audio",
            "volume":1.0)" + field + R"(,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]}]},"trackEffects":[""]})";
    };
    CHECK(ProjectDocument::deserialize(document(""), loaded, error, 48000.0));   // saved before G6
    CHECK(loaded.project.tracks()[0]->volumeEnvelope.points().empty());
    CHECK(!loaded.project.tracks()[0]->volumeEnvelope.shown());
    // Points saved before G6b have no shape: straight lines. An unknown shape does not load.
    CHECK(ProjectDocument::deserialize(document(R"(,"volumeEnvelope":{"points":[{"position":"0","level":0.5}]})"), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->volumeEnvelope.points()[0].shape == automation::CurveShape::Linear);
    CHECK(!ProjectDocument::deserialize(document(R"(,"volumeEnvelope":{"points":[{"position":"0","level":0.5,"shape":"bezier"}]})"), loaded, error, 48000.0));
    CHECK(!ProjectDocument::deserialize(document(R"(,"volumeEnvelope":{"points":[{"position":"0","level":0.5,"shape":3}]})"), loaded, error, 48000.0));
        // Saved shown with no points: opens as saved (the start point comes only from showing it).
    CHECK(ProjectDocument::deserialize(document(R"(,"volumeEnvelope":{"points":[],"shown":true})"), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->volumeEnvelope.shown());
    CHECK(loaded.project.tracks()[0]->volumeEnvelope.points().empty());
    for (const char* bad : {R"(,"volumeEnvelope":[])", R"(,"volumeEnvelope":{})",
                            R"(,"volumeEnvelope":{"points":[{"position":10,"level":0.5}]})",
                            R"(,"volumeEnvelope":{"points":[{"position":"-1","level":0.5}]})",
                            R"(,"volumeEnvelope":{"points":[{"position":"x","level":0.5}]})",
                            R"(,"volumeEnvelope":{"points":[{"position":"5","level":1.5}]})",
                            R"(,"volumeEnvelope":{"points":[{"position":"5","level":0.5},{"position":"5","level":0.5}]})",
                            R"(,"volumeEnvelope":{"points":[],"bypassed":"yes"})"}) {
        error.clear();
        CHECK(!ProjectDocument::deserialize(document(bad), loaded, error, 48000.0));
        CHECK(error == "Invalid volume envelope.");
    }
}

TEST(projectdocument, mixing_plugin_settings_round_trip_and_are_optional)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& track = project.addTrack(TrackKind::Audio, "Vox");
    auto settings = mixfx::defaults(mixfx::Compressor);
    settings.values["threshold"] = -24;
    settings.bypassed = true;
    CHECK(project.setTrackMixEffect(track.id, mixfx::Compressor, settings));
    CHECK(!project.setTrackMixEffect(track.id, "eq", settings));
    const std::string json = ProjectDocument::serialize(project, {}, {""});

    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(json, loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->mixEffect(mixfx::Compressor) == settings);

    const auto document = [](const std::string& field) {
        return R"({"format":"ChaosMakerProject","version":2,"project":{"version":1,"bpm":120.0,
            "sampleRate":48000.0,"numerator":4,"denominator":4,"tracks":[{"name":"T1","type":"Audio",
            "volume":1.0)" + field + R"(,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]}]},"trackEffects":[""]})";
    };
    CHECK(ProjectDocument::deserialize(document(""), loaded, error, 48000.0));   // saved before G7
    CHECK(loaded.project.tracks()[0]->mixEffects.empty());
    CHECK(loaded.project.tracks()[0]->mixEffect(mixfx::Compressor) == mixfx::defaults(mixfx::Compressor));
    for (const char* bad : {R"(,"mixEffects":[])", R"(,"mixEffects":{"flanger":{}})",
                            R"(,"mixEffects":{"compressor":{"values":{"ratio":"x"}}})"})
        CHECK(!ProjectDocument::deserialize(document(bad), loaded, error, 48000.0));
}

TEST(projectdocument, the_master_tracks_effects_round_trip_and_are_optional)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    project.addTrack(TrackKind::Audio, "Vox");
    const int master = Project::MasterTrackId;
    CHECK(project.setTrackEffects(master, {"eq", "compressor", "limiter"}));
    CHECK(project.setTrackEqBand(master, 2, -4.5f));
    auto limiter = mixfx::defaults(mixfx::Limiter);
    limiter.values["ceiling"] = -2;
    CHECK(project.setTrackMixEffect(master, mixfx::Limiter, limiter));
    const std::string json = ProjectDocument::serialize(project, {}, {""});

    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(json, loaded, error, 48000.0));
    const auto& back = loaded.project.master();
    CHECK(back.effects.size() == 3 && back.effects[2].type == "limiter");
    CHECK(back.eqGainDb[2] == -4.5f);
    CHECK(back.mixEffect(mixfx::Limiter).values.at("ceiling") == -2.0f);
    CHECK(loaded.project.tracks().size() == 1);

    const auto document = [](const std::string& field) {
        return R"({"format":"ChaosMakerProject","version":2,"project":{"version":1,"bpm":120.0,
            "sampleRate":48000.0,"numerator":4,"denominator":4)" + field + R"(,"tracks":[{"name":"T1","type":"Audio",
            "volume":1.0,"eq":[0,0,0,0,0,0,0,0,0,0],"clips":[]}]},"trackEffects":[""]})";
    };
    CHECK(ProjectDocument::deserialize(document(""), loaded, error, 48000.0));   // saved before G9
    CHECK(loaded.project.master().effects.empty());
    for (const char* bad : {R"(,"master":[])", R"(,"master":{"effects":["guitarsuite"]})",
                            R"(,"master":{"effects":["eq","eq"]})", R"(,"master":{"eq":[1,2]})",
                            R"(,"master":{"mixEffects":{"limiter":{"values":{"ceiling":"x"}}}})"})
        CHECK(!ProjectDocument::deserialize(document(bad), loaded, error, 48000.0));
}

TEST(projectdocument, clip_tool_flags_round_trip_and_are_optional)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& track = project.addTrack(TrackKind::Audio, "Vox");
    AudioClip clip;
    clip.poolId = "take";
    clip.length = 1000;
    clip.reversed = true;
    clip.polarityInverted = true;
    CHECK(project.addAudioClip(track.id, clip) > 0);
    TrackList::MediaPaths paths;
    paths.insert("take");
    const std::string json = ProjectDocument::serialize(project, paths, {""});
    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(json, loaded, error, 48000.0));
    const auto& back = loaded.project.tracks()[0]->audioClips[0];
    CHECK(back.reversed && back.polarityInverted);
    auto text = nlohmann::json::parse(json);
    auto& c = text["project"]["tracks"][0]["clips"][0];
    c.erase("reversed"); c.erase("polarityInverted");           // saved before G14
    CHECK(ProjectDocument::deserialize(text.dump(), loaded, error, 48000.0));
    CHECK(!loaded.project.tracks()[0]->audioClips[0].reversed && !loaded.project.tracks()[0]->audioClips[0].polarityInverted);
    c["reversed"] = "yes";
    CHECK(!ProjectDocument::deserialize(text.dump(), loaded, error, 48000.0));
}

TEST(projectdocument, clip_effects_round_trip_and_are_optional)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& track = project.addTrack(TrackKind::Audio, "Vox");
    AudioClip clip;
    clip.poolId = "take";
    clip.length = 1000;
    const int id = project.addAudioClip(track.id, clip);
    CHECK(project.setTrackEffects(Project::clipEffectsId(id), {"chaoseq", "compressor"}));
    auto compressor = mixfx::defaults(mixfx::Compressor);
    compressor.values["ratio"] = 7;
    CHECK(project.setTrackMixEffect(Project::clipEffectsId(id), mixfx::Compressor, compressor));
    TrackList::MediaPaths paths;
    paths.insert("take");
    const std::string json = ProjectDocument::serialize(project, paths, {""});
    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(json, loaded, error, 48000.0));
    const auto& back = loaded.project.tracks()[0]->audioClips[0];
    CHECK(back.fx.effects.size() == 2 && back.fx.effects[1].type == "compressor");
    CHECK(back.fx.mixEffect(mixfx::Compressor).values.at("ratio") == 7.0f);
    auto text = nlohmann::json::parse(json);
    auto& c = text["project"]["tracks"][0]["clips"][0];
    c["effects"] = {{"effects", {"guitarsuite"}}};
    CHECK(!ProjectDocument::deserialize(text.dump(), loaded, error, 48000.0));
    c.erase("effects");                                         // saved before R2
    CHECK(ProjectDocument::deserialize(text.dump(), loaded, error, 48000.0));
    CHECK(!loaded.project.tracks()[0]->audioClips[0].fx.hasEffects());
}

TEST(projectdocument, clip_fades_round_trip_are_optional_and_validated)
{
    // restMap G8.
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& track = project.addTrack(TrackKind::Audio, "Vox");
    AudioClip clip;
    clip.poolId = "take";
    clip.length = 1000;
    clip.fadeIn = 200;
    clip.fadeOut = 300;
    CHECK(project.addAudioClip(track.id, clip) > 0);
    TrackList::MediaPaths paths;
    paths.insert("take");
    const std::string json = ProjectDocument::serialize(project, paths, {""});
    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(json, loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->audioClips[0].fadeIn == 200);
    CHECK(loaded.project.tracks()[0]->audioClips[0].fadeOut == 300);
    CHECK(ProjectDocument::deserialize(json, loaded, error, 96000.0));         // rescaled like the clip
    CHECK(loaded.project.tracks()[0]->audioClips[0].fadeIn == 400);
    auto text = nlohmann::json::parse(json);
    auto& c = text["project"]["tracks"][0]["clips"][0];
    c.erase("fadeIn"); c.erase("fadeOut");                                      // saved before G8
    CHECK(ProjectDocument::deserialize(text.dump(), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->audioClips[0].fadeIn == 0 && loaded.project.tracks()[0]->audioClips[0].fadeOut == 0);
    c["fadeIn"] = "5000";                                                       // longer than the clip
    CHECK(!ProjectDocument::deserialize(text.dump(), loaded, error, 48000.0));
    c["fadeIn"] = 20;                                                           // not a string
    CHECK(!ProjectDocument::deserialize(text.dump(), loaded, error, 48000.0));
}

TEST(projectdocument, clip_takes_round_trip_rescale_with_the_rate_and_are_optional)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& track = project.addTrack(TrackKind::Audio, "Vox");
    AudioClip clip;
    clip.poolId = "take1";
    clip.length = 1000;
    MidiNote note; note.id = 1; note.note = 60; note.startBeat = 0.5; note.lengthBeats = 1; note.velocity = 90;
    cliptakes::set(clip, {ClipTake{}, ClipTake{"take2", true, 480.0, {}}, ClipTake{"take3", false, -96.0, {note}}}, 0);
    CHECK(project.addAudioClip(track.id, clip) > 0);
    TrackList::MediaPaths paths{"take1", "take2"};
    const std::string json = ProjectDocument::serialize(project, paths, {""});
    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(json, loaded, error, 48000.0));
    const auto& back = loaded.project.tracks()[0]->audioClips[0];
    CHECK(back.takes == clip.takes);
    CHECK(back.takes.list[1].reversed && back.takes.list[2].notes.size() == 1 && back.takes.list[2].notes[0].startBeat == 0.5);
    CHECK(loaded.mediaPaths.contains("take2") && !loaded.mediaPaths.contains("take3"));
    CHECK(loaded.warning.find("take2") != std::string::npos);      // its file is missing here

    // Another rate: leads are project samples, so they scale like clip starts.
    CHECK(ProjectDocument::deserialize(json, loaded, error, 96000.0));
    CHECK(loaded.project.tracks()[0]->audioClips[0].takes.list[1].lead == 960.0);

    auto text = nlohmann::json::parse(json);
    auto& c = text["project"]["tracks"][0]["clips"][0];
    for (auto bad : {nlohmann::json{{"active", 1}, {"list", c["takes"]["list"]}},          // active entry not null
                     nlohmann::json{{"active", 0}, {"list", nlohmann::json::array({nullptr})}},   // one take
                     nlohmann::json{{"active", 0}, {"list", {nullptr, {{"path", "x"}}}}},         // no lead
                     nlohmann::json("yes")}) {
        auto broken = text;
        broken["project"]["tracks"][0]["clips"][0]["takes"] = bad;
        CHECK(!ProjectDocument::deserialize(broken.dump(), loaded, error, 48000.0));
    }
    c.erase("takes");                                              // saved before G11
    CHECK(ProjectDocument::deserialize(text.dump(), loaded, error, 48000.0));
    CHECK(loaded.project.tracks()[0]->audioClips[0].takes.list.empty());
}
