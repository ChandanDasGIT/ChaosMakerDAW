#include "projectdocument.h"
#include "recording/takerecorder.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <fstream>

namespace persistence {

using json = nlohmann::json;

std::string ProjectDocument::serialize(const Project& project,
                                       const TrackList::MediaPaths& mediaPaths,
                                       const std::vector<std::string>& trackEffectsJson,
                                       std::string_view format,
                                       int version,
                                       const std::filesystem::path& baseDirectory)
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
        for (const auto& clip : track->audioClips) {
            std::string clipPath = clip.poolId;
            if (!baseDirectory.empty()) {
                std::error_code ec;
                auto rel = std::filesystem::relative(clipPath, baseDirectory, ec);
                if (!ec && !rel.empty() && !rel.string().starts_with("..")) {
                    clipPath = rel.string();
                }
            }
            json clipObj;
            clipObj["path"] = clipPath;
            clipObj["hasAudio"] = mediaPaths.contains(clip.poolId);
            clipObj["start"] = std::to_string(clip.start);
            clipObj["length"] = std::to_string(clip.length);
            clipObj["offset"] = std::to_string(clip.sourceOffset);
            clipObj["gainDb"] = clip.gainDb;
            clipObj["effectsPrinted"] = clip.effectsPrinted;
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
        trackObj["input"] = track->inputPort;
        trackObj["recordDry"] = track->recordDry;
        trackObj["eq"] = eqJson;
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
        track.inputPort = item.value("input", "");

        if (!item.contains("eq") || !item["eq"].is_array() || item["eq"].size() != 10
            || track.volume < 0.0f || track.volume > 2.0f
            || !item.contains("clips") || !item["clips"].is_array()) {
            error = "Invalid track controls.";
            return false;
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

            clip.sourceOffset = offset;
            clip.start = std::llround(start * rate / oldRate);
            clip.length = std::max<std::int64_t>(1, std::llround(length * rate / oldRate));

            std::string pathStr = c.value("path", "");
            std::string resolvedPath = pathStr;
            if (!baseDirectory.empty() && !pathStr.empty()) {
                std::error_code ec;
                auto candidatePath = baseDirectory / pathStr;
                if (std::filesystem::exists(candidatePath, ec)) {
                    resolvedPath = candidatePath.string();
                }
            }
            clip.poolId = resolvedPath;
            clip.effectsPrinted = c.value("effectsPrinted", true);
            clip.gainDb = c.value("gainDb", 0.0f);

            if (!std::isfinite(clip.gainDb) || clip.gainDb < -120.0f || clip.gainDb > 24.0f) {
                error = "Invalid clip gain.";
                return false;
            }

            const bool hasAudio = c.value("hasAudio", false);
            if (hasAudio) {
                mediaPaths.insert(resolvedPath);
                if (!audio.contains(resolvedPath)) {
                    try {
                        audio[resolvedPath] = loadTakeWav(resolvedPath);
                    } catch (...) {
                        missing.push_back(std::filesystem::path(resolvedPath).filename().string());
                    }
                }
            }

            if (candidate.addAudioClip(track.id, std::move(clip)) < 0) {
                error = "Invalid clip.";
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
                                 std::string* error)
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

    const std::string jsonStr = serialize(project, mediaPaths, trackEffectsJson, format, currentVersion, parentDir);

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
