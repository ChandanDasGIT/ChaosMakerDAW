#pragma once

#include "session/project.h"
#include "session/tracklist.h"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace persistence {

struct ProjectFileContent {
    Project project;
    TrackList::AudioMap audio;
    TrackList::MediaPaths mediaPaths;
    std::vector<std::string> trackEffectsJson;
    std::string warning;
};

class ProjectDocument {
public:
    static constexpr std::string_view projectFormat = "ChaosMakerProject";
    static constexpr std::string_view sessionFormat = "ChaosMakerSession";
    static constexpr int currentVersion = 2;
    static constexpr std::size_t maxFileSize = 16 * 1024 * 1024; // 16 MB

    static std::string serialize(const Project& project,
                                 const TrackList::MediaPaths& mediaPaths,
                                 const std::vector<std::string>& trackEffectsJson,
                                 std::string_view format = projectFormat,
                                 int version = currentVersion,
                                 const std::filesystem::path& baseDirectory = {},
                                 const std::unordered_map<std::string, std::string>* pathMap = nullptr);

    static bool deserialize(std::string_view jsonStr,
                            ProjectFileContent& outContent,
                            std::string& error,
                            double targetSampleRate = 48000.0,
                            const std::filesystem::path& baseDirectory = {});

    static bool saveToFile(const std::filesystem::path& filePath,
                           const Project& project,
                           const TrackList::MediaPaths& mediaPaths,
                           const std::vector<std::string>& trackEffectsJson,
                           std::string_view format = projectFormat,
                           std::string* error = nullptr,
                           // Clip files to save under other paths (restMap E4: copies beside the project).
                           const std::unordered_map<std::string, std::string>* pathMap = nullptr);

    static bool loadFromFile(const std::filesystem::path& filePath,
                             ProjectFileContent& outContent,
                             std::string& error,
                             double targetSampleRate = 48000.0);
};

} // namespace persistence
