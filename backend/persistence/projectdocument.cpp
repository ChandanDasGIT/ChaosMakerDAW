#include "projectdocument.h"
#include "chaoseqjson.h"
#include "mixfxjson.h"
#include "effectstatejson.h"
#include "drumkitjson.h"
#include "electrodrumsjson.h"
#include "pianojson.h"
#include "chordsjson.h"
#include "harmoniumjson.h"
#include "mixer/mixerpanel.h"
#include "recording/takerecorder.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

namespace persistence {

using json = nlohmann::json;

namespace {
// Pattern clips keep sourceOffset in project samples; this converts it to beats.
double offsetBeats(const TempoMap& tempo, std::int64_t sourceOffset)
{
    return double(sourceOffset) * tempo.bpm() / (60.0 * tempo.sampleRate());
}
}

std::string ProjectDocument::serialize(const Project& project,
                                       const TrackList::MediaPaths& mediaPaths,
                                       const std::vector<std::string>& trackEffectsJson,
                                       std::string_view format,
                                       int version,
                                       const std::filesystem::path& baseDirectory,
                                       const std::unordered_map<std::string, std::string>* pathMap)
{
    json doc;
    doc["format"] = std::string(format);
    doc["version"] = version;

    json tracksJson = json::array();
    for (const auto& track : project.tracks()) {
        json eqJson = json::array();
        for (float v : track->eqGainDb) {
            eqJson.push_back(v);
        }
        json clipsJson = json::array();
        // A file as the project stores it: moved by a media collect, relative when inside.
        const auto savedPath = [&](const std::string& poolId) {
            std::string path = poolId;
            if (pathMap) {
                if (const auto moved = pathMap->find(poolId); moved != pathMap->end()) path = moved->second;
            }
            if (!baseDirectory.empty()) {
                std::error_code ec;
                auto rel = std::filesystem::relative(path, baseDirectory, ec);
                if (!ec && !rel.empty() && !rel.string().starts_with("..")) path = rel.string();
            }
            return path;
        };
        for (const auto& clip : track->audioClips) {
            json clipObj;
            clipObj["path"] = savedPath(clip.poolId);
            clipObj["hasAudio"] = mediaPaths.contains(clip.poolId);
            clipObj["start"] = std::to_string(clip.start);
            clipObj["length"] = std::to_string(clip.length);
            clipObj["offset"] = std::to_string(clip.sourceOffset);
            clipObj["gainDb"] = clip.gainDb;
            clipObj["effectsPrinted"] = clip.effectsPrinted;
            if (clip.fadeIn > 0) clipObj["fadeIn"] = std::to_string(clip.fadeIn);      // restMap G8
            if (clip.fadeOut > 0) clipObj["fadeOut"] = std::to_string(clip.fadeOut);
            if (clip.reversed) clipObj["reversed"] = true;                   // restMap G14
            if (clip.polarityInverted) clipObj["polarityInverted"] = true;
            if (clip.fx.hasEffects()) clipObj["effects"] = effectstatejson::toJson(clip.fx);   // restMap R2
            if (!clip.takes.list.empty()) {   // restMap G11: the other takes; null = the clip itself
                json list = json::array();
                for (std::size_t i = 0; i < clip.takes.list.size(); ++i) {
                    if (int(i) == clip.takes.active) { list.push_back(nullptr); continue; }
                    const auto& take = clip.takes.list[i];
                    json takeObj{{"path", savedPath(take.poolId)}, {"hasAudio", mediaPaths.contains(take.poolId)},
                                 {"lead", take.lead}};
                    if (take.reversed) takeObj["reversed"] = true;
                    if (!take.notes.empty()) {
                        json notesJson = json::array();
                        for (const auto& n : take.notes)
                            notesJson.push_back({{"pitch", n.note}, {"start", n.startBeat},
                                                 {"length", n.lengthBeats}, {"velocity", n.velocity}});
                        takeObj["notes"] = std::move(notesJson);
                    }
                    list.push_back(std::move(takeObj));
                }
                clipObj["takes"] = {{"active", clip.takes.active}, {"list", std::move(list)}};
            }
            if (!clip.notes.empty()) {
                // Stored clip-relative (0 = clip start) so notes stay on the clip even if the
                // project is reopened at another sample rate.
                const double offset = offsetBeats(project.tempo(), clip.sourceOffset);
                json notesJson = json::array();
                for (const auto& n : clip.notes) {
                    notesJson.push_back({{"pitch", n.note}, {"start", n.startBeat - offset},
                                         {"length", n.lengthBeats}, {"velocity", n.velocity}});
                }
                clipObj["notes"] = std::move(notesJson);
            }
            clipsJson.push_back(clipObj);
        }

        json trackObj;
        trackObj["name"] = track->name;
        trackObj["colour"] = track->colour;
        trackObj["type"] = track->type;
        trackObj["instrument"] = (track->kind == TrackKind::Instrument);
        trackObj["solo"] = track->solo;
        trackObj["muted"] = track->muted;
        trackObj["volume"] = track->volume;
        trackObj["pan"] = track->pan;
        trackObj["stripWidth"] = track->stripWidth;
        trackObj["input"] = track->inputPort;
        trackObj["recordDry"] = track->recordDry;
        trackObj["eq"] = eqJson;
        trackObj["chaosEq"] = chaoseqjson::toJson(track->chaosEq);   // plan 6.2
        if (!track->mixEffects.empty()) {   // restMap G7
            json mix = json::object();
            for (const auto& [type, settings] : track->mixEffects) mix[type] = mixfxjson::toJson(settings);
            trackObj["mixEffects"] = std::move(mix);
        }
        if (track->type == "Harmonium") trackObj["harmonium"] = harmoniumjson::toJson(track->harmonium);
        if (track->type == "Drums") trackObj["drumkit"] = drumkitjson::toJson(track->drumkit);
        if (track->type == "Electronic Drums")
            trackObj["electrodrums"] = electrodrumsjson::toJson(track->electrodrums);
        if (track->type == "Piano" || track->type == "Chords") trackObj["piano"] = pianojson::toJson(track->piano);
        if (track->type == "Chords") trackObj["chords"] = chordsjson::toJson(track->chords);
        json effectsList = json::array();
        for (const auto& slot : track->effects) effectsList.push_back(slot.type);
        trackObj["effects"] = std::move(effectsList);   // insert effects in chain order (plan 6.1)
        {   // restMap G6; positions as strings like clip timing (64-bit samples)
            const auto& envelope = track->volumeEnvelope;
            json pointsJson = json::array();
            for (const auto& p : envelope.points())
                pointsJson.push_back({{"position", std::to_string(p.position)}, {"level", p.level},
                                      {"shape", std::string(automation::curveShapeId(p.shape))}});
            trackObj["volumeEnvelope"] = {{"points", std::move(pointsJson)}, {"bypassed", envelope.bypassed()},
                                          {"shown", envelope.shown()}, {"overClips", envelope.overClips()}};
        }
        trackObj["clips"] = clipsJson;
        tracksJson.push_back(trackObj);
    }

    const auto& tempo = project.tempo();
    json projectJson;
    projectJson["version"] = 1;
    projectJson["bpm"] = tempo.bpm();
    projectJson["numerator"] = tempo.numerator();
    projectJson["denominator"] = tempo.denominator();
    projectJson["sampleRate"] = tempo.sampleRate();
    projectJson["masterVolume"] = project.masterVolume();
    projectJson["masterMuted"] = project.masterMuted();
    projectJson["master"] = effectstatejson::toJson(project.master());   // restMap G9
    projectJson["tracks"] = tracksJson;

    doc["project"] = projectJson;

    json effectsJson = json::array();
    for (const auto& fxStr : trackEffectsJson) {
        if (!fxStr.empty()) {
            try {
                effectsJson.push_back(json::parse(fxStr));
            } catch (...) {
                effectsJson.push_back(json::object());
            }
        } else {
            effectsJson.push_back(json::object());
        }
    }
    doc["trackEffects"] = effectsJson;

    return doc.dump();
}

bool ProjectDocument::deserialize(std::string_view jsonStr,
                                  ProjectFileContent& outContent,
                                  std::string& error,
                                  double targetSampleRate,
                                  const std::filesystem::path& baseDirectory)
{
    json doc;
    try {
        doc = json::parse(jsonStr);
    } catch (...) {
        error = "Invalid project file; current project kept.";
        return false;
    }

    if (!doc.is_object()) {
        error = "Invalid project file; current project kept.";
        return false;
    }

    const std::string fmt = doc.value("format", "");
    if (fmt != projectFormat && fmt != sessionFormat) {
        error = "Invalid project file; current project kept.";
        return false;
    }

    const int version = doc.value("version", 0);
    if (version != 1 && version != 2) {
        error = "Invalid project file; current project kept.";
        return false;
    }

    if (!doc.contains("project") || !doc["project"].is_object()) {
        error = "Invalid project file; current project kept.";
        return false;
    }

    const auto& projectJson = doc["project"];
    if (!projectJson.contains("tracks") || !projectJson["tracks"].is_array()) {
        error = "Invalid session document.";
        return false;
    }

    const auto& tracksJson = projectJson["tracks"];
    if (tracksJson.size() > 1024) {
        error = "Too many session tracks.";
        return false;
    }

    if (version == 1) {
        if (!doc.contains("guitarSuite") || !doc["guitarSuite"].is_object()) {
            error = "Invalid project file; current project kept.";
            return false;
        }
    } else if (version == 2) {
        if (!doc.contains("trackEffects") || !doc["trackEffects"].is_array()
            || doc["trackEffects"].size() != tracksJson.size()) {
            error = "Invalid project file; current project kept.";
            return false;
        }
    }

    const double bpm = projectJson.value("bpm", -1.0);
    const double oldRate = projectJson.value("sampleRate", -1.0);
    const int numerator = projectJson.value("numerator", 0);
    const int denominator = projectJson.value("denominator", 0);

    if (projectJson.value("version", 0) != 1 || bpm < 0.01 || bpm > 10000.0
        || oldRate < 1000.0 || oldRate > 768000.0 || numerator <= 0 || denominator <= 0) {
        error = "Invalid session document.";
        return false;
    }

    const double rate = (targetSampleRate > 1000.0) ? targetSampleRate : 48000.0;
    Project candidate;
    candidate.tempo().set(bpm, numerator, denominator, rate);
    candidate.setMasterVolume(projectJson.value("masterVolume", 1.0f));
    candidate.setMasterMuted(projectJson.value("masterMuted", false));
    // The master track's effects (restMap G9); projects saved before have none.
    if (projectJson.contains("master")) {
        if (!effectstatejson::apply(projectJson["master"], candidate, Project::MasterTrackId, error)) {
            error = "Invalid master effects: " + error;
            return false;
        }
    }

    TrackList::AudioMap audio;
    TrackList::MediaPaths mediaPaths;
    std::vector<std::string> missing;

    for (const auto& item : tracksJson) {
        if (!item.is_object()) {
            error = "Invalid session track.";
            return false;
        }
        const bool isInstrument = item.value("instrument", false);
        const std::string name = item.value("name", "");
        const std::string type = item.value("type", "");

        auto& track = candidate.addTrack(isInstrument ? TrackKind::Instrument : TrackKind::Audio, name, type);
        track.recordDry = item.value("recordDry", track.type == "Audio");

        if (item.contains("colour")) {
            if (!item["colour"].is_string() || !candidate.setTrackColour(track.id, item["colour"].get<std::string>())) {
                error = "Invalid track colour.";
                return false;
            }
        }

        track.solo = item.value("solo", false);
        track.muted = item.value("muted", false);
        track.volume = item.value("volume", -1.0f);
        // Optional: projects saved before pan existed load centred.
        if (item.contains("pan") && !item["pan"].is_number()) {
            error = "Invalid track controls.";
            return false;
        }
        track.pan = item.value("pan", 0.0f);
        // Optional too: older projects keep the default mixer strip width.
        if (item.contains("stripWidth") && !item["stripWidth"].is_number()) {
            error = "Invalid track controls.";
            return false;
        }
        track.stripWidth = item.value("stripWidth", Track{}.stripWidth);
        track.inputPort = item.value("input", "");

        if (!item.contains("eq") || !item["eq"].is_array() || item["eq"].size() != 10
            || track.volume < 0.0f || track.volume > 2.0f
            || !std::isfinite(track.pan) || track.pan < -1.0f || track.pan > 1.0f
            || !std::isfinite(track.stripWidth) || track.stripWidth < mixer::minStripWidth
            || track.stripWidth > mixer::maxStripWidth
            || !item.contains("clips") || !item["clips"].is_array()) {
            error = "Invalid track controls.";
            return false;
        }

        // Optional: files without it (saved before plan 6.1) keep the track type's defaults.
        if (item.contains("effects")) {
            std::vector<std::string> types;
            bool valid = item["effects"].is_array();
            if (valid)
                for (const auto& e : item["effects"]) {
                    if (!e.is_string()) { valid = false; break; }
                    types.push_back(e.get<std::string>());
                }
            if (!valid || !candidate.setTrackEffects(track.id, types)) {
                error = "Invalid track effects.";
                return false;
            }
        }

        // Optional: files saved before the mixing plugins (restMap G7) have none.
        if (item.contains("mixEffects")) {
            const auto& mix = item["mixEffects"];
            if (!mix.is_object()) { error = "Invalid mixing effect settings."; return false; }
            for (const auto& [type, settingsJson] : mix.items()) {
                mixfx::Settings settings;
                if (!mixfxjson::fromJson(type, settingsJson, settings, error)) return false;
                candidate.setTrackMixEffect(track.id, type, settings);
            }
        }

        // Optional: files saved before Chaos EQ existed load with no bands.
        if (item.contains("chaosEq")) {
            chaoseq::Settings chaos;
            if (!chaoseqjson::fromJson(item["chaosEq"], chaos, error)) return false;
            candidate.setTrackChaosEq(track.id, chaos);
        }
        if (item.contains("harmonium")) {
            harmonium::Settings settings;
            if (!harmoniumjson::fromJson(item["harmonium"], settings, error)) return false;
            candidate.setTrackHarmonium(track.id, settings);
        }
        if (item.contains("drumkit")) {
            drumkit::Settings settings;
            if (!drumkitjson::fromJson(item["drumkit"], settings, error)) return false;
            candidate.setTrackDrumKit(track.id, settings);
        }
        if (item.contains("electrodrums")) {
            electrodrums::Settings settings;
            if (!electrodrumsjson::fromJson(item["electrodrums"], settings, error)) return false;
            candidate.setTrackElectroDrums(track.id, settings);
        }
        if (item.contains("piano")) {
            piano::Settings settings;
            if (!pianojson::fromJson(item["piano"], settings, error)) return false;
            candidate.setTrackPiano(track.id, settings);
        }
        if (item.contains("chords")) {
            chords::Settings settings;
            if (!chordsjson::fromJson(item["chords"], settings, error)) return false;
            candidate.setTrackChords(track.id, settings);
        }

        // Optional: projects saved before G6 load without an envelope. Points are rescaled like
        // clips when the project reopens at another sample rate.
        if (item.contains("volumeEnvelope")) {
            const auto& e = item["volumeEnvelope"];
            bool valid = e.is_object() && e.contains("points") && e["points"].is_array();
            for (const char* flag : {"bypassed", "shown", "overClips"})
                if (valid && e.contains(flag) && !e[flag].is_boolean()) valid = false;
            std::vector<automation::EnvelopePoint> points;
            if (valid) {
                for (const auto& pj : e["points"]) {
                    if (!pj.is_object() || !pj.contains("position") || !pj["position"].is_string()
                        || !pj.contains("level") || !pj["level"].is_number()) { valid = false; break; }
                    std::int64_t position = -1;
                    try { position = std::stoll(pj["position"].get<std::string>()); } catch (...) { valid = false; break; }
                    if (position < 0 || double(position) > double(INT64_MAX / 4) * std::min(1.0, oldRate / rate)) {
                        valid = false;
                        break;
                    }
                    // "shape" is optional: projects saved before G6b have straight lines.
                    auto shape = automation::CurveShape::Linear;
                    if (pj.contains("shape")) {
                        const auto known = pj["shape"].is_string()
                            ? automation::curveShapeFromId(pj["shape"].get<std::string>()) : std::nullopt;
                        if (!known) { valid = false; break; }
                        shape = *known;
                    }
                    points.push_back({position, pj["level"].get<double>(), shape});
                }
            }
            if (!valid || !track.volumeEnvelope.setPoints(std::move(points))) {
                error = "Invalid volume envelope.";
                return false;
            }
            track.volumeEnvelope.rescale(rate / oldRate);
            track.volumeEnvelope.setBypassed(e.value("bypassed", false));
            track.volumeEnvelope.setShown(e.value("shown", false));
            track.volumeEnvelope.setOverClips(e.value("overClips", false));
        }

        for (int i = 0; i < 10; ++i) {
            const double gain = item["eq"][i].get<double>();
            if (gain < -12.0 || gain > 12.0) {
                error = "Invalid track EQ.";
                return false;
            }
            track.eqGainDb[i] = static_cast<float>(gain);
        }

        for (const auto& c : item["clips"]) {
            if (!c.is_object()) {
                error = "Invalid clip.";
                return false;
            }
            AudioClip clip;
            std::int64_t start = 0, length = 0, offset = 0;
            try {
                start = std::stoll(c.value("start", ""));
                length = std::stoll(c.value("length", ""));
                offset = std::stoll(c.value("offset", ""));
            } catch (...) {
                error = "Invalid clip timing.";
                return false;
            }

            if (start < 0 || length <= 0 || offset < 0
                || (double(start) + double(length)) > double(INT64_MAX / 4) * std::min(1.0, oldRate / rate)) {
                error = "Invalid clip timing.";
                return false;
            }

            std::string pathStr = c.value("path", "");
            const bool hasAudio = c.value("hasAudio", false);
            // A pattern clip has no media: its offset is in project samples, so it is rescaled
            // with start and length. An audio clip's offset is in the file's own samples.
            const bool pattern = !hasAudio && pathStr.starts_with("Clip ");
            clip.sourceOffset = pattern ? std::llround(offset * rate / oldRate) : offset;
            clip.start = std::llround(start * rate / oldRate);
            clip.length = std::max<std::int64_t>(1, std::llround(length * rate / oldRate));
            // Fades (restMap G8), project samples like start; optional, older files have none.
            for (auto [key, into] : {std::pair{"fadeIn", &clip.fadeIn}, std::pair{"fadeOut", &clip.fadeOut}}) {
                if (!c.contains(key)) continue;
                std::int64_t value = -1;
                try { if (c[key].is_string()) value = std::stoll(c[key].get<std::string>()); } catch (...) {}
                if (value < 0 || value > length) { error = "Invalid clip fade."; return false; }
                *into = std::llround(value * rate / oldRate);
            }
            clip.fadeIn = std::min(clip.fadeIn, clip.length);
            clip.fadeOut = std::min(clip.fadeOut, clip.length - clip.fadeIn);

            // A stored path: inside the project's folder when it is there.
            const auto resolve = [&](const std::string& stored) {
                if (!baseDirectory.empty() && !stored.empty()) {
                    std::error_code ec;
                    auto candidatePath = baseDirectory / stored;
                    if (std::filesystem::exists(candidatePath, ec)) return candidatePath.string();
                }
                return stored;
            };
            const auto loadMedia = [&](const std::string& path) {
                mediaPaths.insert(path);
                if (audio.contains(path)) return;
                try {
                    audio[path] = loadTakeWav(path);
                } catch (...) {
                    missing.push_back(std::filesystem::path(path).filename().string());
                }
            };
            const std::string resolvedPath = resolve(pathStr);
            clip.poolId = resolvedPath;
            clip.effectsPrinted = c.value("effectsPrinted", true);
            // Optional (restMap G14): older files play forwards and upright.
            for (const char* flag : {"reversed", "polarityInverted"})
                if (c.contains(flag) && !c[flag].is_boolean()) { error = "Invalid clip."; return false; }
            clip.reversed = c.value("reversed", false);
            clip.polarityInverted = c.value("polarityInverted", false);
            clip.gainDb = c.value("gainDb", 0.0f);

            if (!std::isfinite(clip.gainDb) || clip.gainDb < -120.0f || clip.gainDb > 24.0f) {
                error = "Invalid clip gain.";
                return false;
            }

            if (c.contains("notes")) { // optional; older projects have none
                const auto& notesJson = c["notes"];
                if (!notesJson.is_array()) {
                    error = "Invalid clip notes.";
                    return false;
                }
                const double offset = offsetBeats(candidate.tempo(), clip.sourceOffset);
                int nextNoteId = 1;
                for (const auto& nj : notesJson) {
                    if (!nj.is_object() || !nj.contains("pitch") || !nj["pitch"].is_number_integer()
                        || !nj.contains("start") || !nj["start"].is_number()
                        || !nj.contains("length") || !nj["length"].is_number()
                        || !nj.contains("velocity") || !nj["velocity"].is_number_integer()) {
                        error = "Invalid clip notes.";
                        return false;
                    }
                    MidiNote n;
                    n.id = nextNoteId++;
                    n.note = nj["pitch"].get<int>();
                    n.startBeat = nj["start"].get<double>() + offset;
                    n.lengthBeats = nj["length"].get<double>();
                    n.velocity = nj["velocity"].get<int>();
                    if (n.note < 0 || n.note > 127 || n.velocity < 0 || n.velocity > 127
                        || !std::isfinite(n.startBeat) || !std::isfinite(n.lengthBeats) || n.lengthBeats <= 0) {
                        error = "Invalid clip notes.";
                        return false;
                    }
                    clip.notes.push_back(n);
                }
            }

            if (hasAudio) loadMedia(resolvedPath);

            if (c.contains("takes")) {   // restMap G11; optional, older projects have none
                const auto& takesJson = c["takes"];
                const bool shaped = takesJson.is_object() && takesJson.contains("active") && takesJson["active"].is_number_integer()
                    && takesJson.contains("list") && takesJson["list"].is_array() && takesJson["list"].size() >= 2;
                const int active = shaped ? takesJson["active"].get<int>() : -1;
                if (!shaped || active < 0 || active >= int(takesJson["list"].size()) || !takesJson["list"][std::size_t(active)].is_null()) {
                    error = "Invalid clip takes.";
                    return false;
                }
                std::vector<ClipTake> takes;
                for (const auto& tj : takesJson["list"]) {
                    ClipTake take;
                    if (!tj.is_null()) {
                        if (!tj.is_object() || !tj.contains("path") || !tj["path"].is_string()
                            || !tj.contains("lead") || !tj["lead"].is_number()
                            || (tj.contains("reversed") && !tj["reversed"].is_boolean())
                            || (tj.contains("hasAudio") && !tj["hasAudio"].is_boolean())
                            || (tj.contains("notes") && !tj["notes"].is_array())) {
                            error = "Invalid clip takes.";
                            return false;
                        }
                        take.poolId = resolve(tj["path"].get<std::string>());
                        take.lead = tj["lead"].get<double>() * rate / oldRate;
                        take.reversed = tj.value("reversed", false);
                        if (!std::isfinite(take.lead) || std::abs(take.lead) > double(INT64_MAX / 4)) {
                            error = "Invalid clip takes.";
                            return false;
                        }
                        if (tj.value("hasAudio", false)) loadMedia(take.poolId);
                        int noteId = 1;
                        for (const auto& nj : tj.value("notes", json::array())) {
                            if (!nj.is_object() || !nj.contains("pitch") || !nj["pitch"].is_number_integer()
                                || !nj.contains("start") || !nj["start"].is_number() || !nj.contains("length")
                                || !nj["length"].is_number() || !nj.contains("velocity") || !nj["velocity"].is_number_integer()) {
                                error = "Invalid clip takes.";
                                return false;
                            }
                            MidiNote n;
                            n.id = noteId++;
                            n.note = nj["pitch"].get<int>();
                            n.startBeat = nj["start"].get<double>();
                            n.lengthBeats = nj["length"].get<double>();
                            n.velocity = nj["velocity"].get<int>();
                            if (n.note < 0 || n.note > 127 || n.velocity < 0 || n.velocity > 127
                                || !std::isfinite(n.startBeat) || !std::isfinite(n.lengthBeats) || n.lengthBeats <= 0) {
                                error = "Invalid clip takes.";
                                return false;
                            }
                            take.notes.push_back(n);
                        }
                    }
                    takes.push_back(std::move(take));
                }
                cliptakes::set(clip, std::move(takes), active);
            }

            const int clipId = candidate.addAudioClip(track.id, std::move(clip));
            if (clipId < 0) {
                error = "Invalid clip.";
                return false;
            }
            // Clip effects (restMap R2); optional.
            if (c.contains("effects") && !effectstatejson::apply(c["effects"], candidate, Project::clipEffectsId(clipId), error)) {
                error = "Invalid clip effects: " + error;
                return false;
            }
        }
    }

    candidate.clearHistory();

    std::vector<std::string> effects;
    if (version == 1) {
        const std::string fxDump = doc["guitarSuite"].dump();
        for (std::size_t i = 0; i < tracksJson.size(); ++i) {
            effects.push_back(fxDump);
        }
    } else {
        for (const auto& fxItem : doc["trackEffects"]) {
            effects.push_back(fxItem.dump());
        }
    }
    // A pedalboard only loads onto a track with GuitarSuite on its effect list; others (e.g.
    // pedals saved on Audio tracks before plan 6.1) are dropped: an empty entry is skipped.
    for (std::size_t i = 0; i < effects.size() && i < candidate.tracks().size(); ++i) {
        const auto& chain = candidate.tracks()[i]->effects;
        if (std::none_of(chain.begin(), chain.end(), [](const auto& s) { return s.type == effectchain::GuitarSuite; }))
            effects[i].clear();
    }

    outContent.project = std::move(candidate);
    outContent.audio = std::move(audio);
    outContent.mediaPaths = std::move(mediaPaths);
    outContent.trackEffectsJson = std::move(effects);

    if (!missing.empty()) {
        std::string warn = "Missing audio (clips kept offline): ";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) warn += ", ";
            warn += missing[i];
        }
        outContent.warning = std::move(warn);
    } else {
        outContent.warning.clear();
    }

    return true;
}

bool ProjectDocument::saveToFile(const std::filesystem::path& filePath,
                                 const Project& project,
                                 const TrackList::MediaPaths& mediaPaths,
                                 const std::vector<std::string>& trackEffectsJson,
                                 std::string_view format,
                                 std::string* error,
                                 const std::unordered_map<std::string, std::string>* pathMap)
{
    std::error_code ec;
    auto parentDir = filePath.parent_path();
    if (!parentDir.empty()) {
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            if (error) *error = "Could not create directory: " + ec.message();
            return false;
        }
    }

    const std::string jsonStr = serialize(project, mediaPaths, trackEffectsJson, format, currentVersion, parentDir, pathMap);

    auto tmpPath = filePath;
    tmpPath += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "Could not open temporary file for writing.";
        return false;
    }
    out.write(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
    out.flush();
    if (!out) {
        if (error) *error = "Failed to write project data.";
        out.close();
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    out.close();

    std::filesystem::rename(tmpPath, filePath, ec);
    if (ec) {
        std::filesystem::copy_file(tmpPath, filePath, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmpPath, ec);
    }
    return true;
}

bool ProjectDocument::loadFromFile(const std::filesystem::path& filePath,
                                   ProjectFileContent& outContent,
                                   std::string& error,
                                   double targetSampleRate)
{
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec) || !std::filesystem::is_regular_file(filePath, ec)) {
        error = "File does not exist or is not a regular file.";
        return false;
    }
    const auto fileSize = std::filesystem::file_size(filePath, ec);
    if (ec || fileSize > maxFileSize) {
        error = "File exceeds maximum allowed size or cannot read size.";
        return false;
    }
    std::ifstream in(filePath, std::ios::binary);
    if (!in) {
        error = "Cannot open file for reading.";
        return false;
    }
    std::string buffer(fileSize, '\0');
    in.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    if (!in && !in.eof()) {
        error = "Error reading file content.";
        return false;
    }
    return deserialize(buffer, outContent, error, targetSampleRate, filePath.parent_path());
}

} // namespace persistence
