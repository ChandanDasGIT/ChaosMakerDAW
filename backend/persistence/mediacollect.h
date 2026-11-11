#pragma once

#include "session/project.h"
#include "session/tracklist.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// Keeps a saved project together with its audio (restMap E4): the files its clips play
// (recordings and imports kept in the app's data folder, or anywhere else) that are outside
// the project's folder are copied into "<project name> Media" beside the project file, and the
// project is saved pointing at the copies by relative paths, so the folder can be moved or
// sent elsewhere whole. Files already inside the project's folder stay where they are; a copy
// made by an earlier save (the same name and bytes) is reused. The open session keeps playing the
// files it has. Runs off the audio thread.
namespace persistence {

struct MediaCollection {
    std::unordered_map<std::string, std::string> paths;   // the clip's file -> its copy
    std::vector<std::string> problems;                    // files that could not be copied (kept as they were)
};

MediaCollection collectMedia(const Project& project, const TrackList::MediaPaths& media,
                             const std::filesystem::path& projectFile);

} // namespace persistence
