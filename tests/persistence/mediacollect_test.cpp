#include "testkit.h"
#include "persistence/mediacollect.h"
#include "persistence/projectdocument.h"
#include "session/projectsession.h"
#include "audiofile/wavwriter.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <unistd.h>

// A saved project keeps its audio with it (restMap E4).
namespace {
namespace fs = std::filesystem;

struct TempDir {
    fs::path path = fs::temp_directory_path() / ("chaosmaker-mediacollect-" + std::to_string(getpid()));
    TempDir() { fs::remove_all(path); fs::create_directories(path); }
    ~TempDir() { fs::remove_all(path); }
};

std::string take(const fs::path& file, int frames)
{
    fs::create_directories(file.parent_path());
    audiofile::writeWav(file.string(), {std::vector<float>(std::size_t(frames), 0.25f)}, 48000, audiofile::WavFormat::Float32);
    return file.string();
}

void addClip(Project& project, int trackId, const std::string& path, std::int64_t start)
{
    AudioClip clip;
    clip.start = start;
    clip.length = 1000;
    clip.poolId = path;
    project.addAudioClip(trackId, clip);
}

std::vector<std::string> savedPaths(const fs::path& projectFile)
{
    std::ifstream f(projectFile);
    const auto doc = nlohmann::json::parse(f);
    std::vector<std::string> paths;
    for (const auto& track : doc["project"]["tracks"])
        for (const auto& clip : track["clips"]) paths.push_back(clip["path"].get<std::string>());
    return paths;
}
}

TEST(mediacollect, a_saved_project_takes_its_audio_along_and_opens_after_a_move)
{
    TempDir dir;
    // Two takes with the same name in the app's data, and one already in the project's folder.
    const auto recorded = take(dir.path / "appdata" / "Recordings" / "take.wav", 2000);
    const auto imported = take(dir.path / "elsewhere" / "take.wav", 3000);
    const auto local = take(dir.path / "Song" / "inside.wav", 1500);

    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& track = project.addTrack(TrackKind::Audio, "Vox");
    addClip(project, track.id, recorded, 0);
    addClip(project, track.id, imported, 2000);
    addClip(project, track.id, recorded, 6000);   // the same file twice: one copy
    addClip(project, track.id, local, 9000);
    const TrackList::MediaPaths media{recorded, imported, local};

    const fs::path file = dir.path / "Song" / "Song.chaosmaker";
    ProjectSession session;
    std::string error, warning;
    CHECK(session.saveProjectFile(file.string(), project, media, {std::string()}, &error, &warning));
    CHECK(error.empty() && warning.empty());
    const fs::path folder = dir.path / "Song" / "Song Media";
    CHECK(fs::exists(folder / "take.wav") && fs::exists(folder / "take-2.wav"));
    CHECK(fs::file_size(folder / "take.wav") != fs::file_size(folder / "take-2.wav"));
    CHECK(!fs::exists(folder / "inside.wav"));                                   // already with the project
    std::size_t files = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(folder)) ++files;
    CHECK(files == 2);
    const auto paths = savedPaths(file);
    CHECK(paths.size() == 4);
    CHECK(paths[0] == "Song Media/take.wav" && paths[1] == "Song Media/take-2.wav");
    CHECK(paths[2] == "Song Media/take.wav" && paths[3] == "inside.wav");
    CHECK(project.tracks()[0]->audioClips[0].poolId == recorded);               // the open session is unchanged

    // Saving again reuses the copies.
    CHECK(session.saveProjectFile(file.string(), project, media, {std::string()}, &error, &warning));
    files = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(folder)) ++files;
    CHECK(files == 2 && savedPaths(file) == paths);

    // A different file of the same name and size is not mistaken for a copy.
    Project other;
    auto& otherTrack = other.addTrack(TrackKind::Audio, "Gtr");
    const auto lookalike = dir.path / "third" / "take.wav";
    fs::create_directories(lookalike.parent_path());
    audiofile::writeWav(lookalike.string(), {std::vector<float>(2000, -0.75f)}, 48000, audiofile::WavFormat::Float32);
    CHECK(fs::file_size(lookalike) == fs::file_size(folder / "take.wav"));
    addClip(other, otherTrack.id, lookalike.string(), 0);
    const auto again = persistence::collectMedia(other, {lookalike.string()}, file);
    CHECK(again.paths.at(lookalike.string()) == (folder / "take-3.wav").string());

    // Moved elsewhere with the originals gone: it opens with all its audio.
    fs::remove_all(dir.path / "appdata");
    fs::remove_all(dir.path / "elsewhere");
    fs::rename(dir.path / "Song", dir.path / "Moved");
    persistence::ProjectFileContent content;
    CHECK(persistence::ProjectDocument::loadFromFile(dir.path / "Moved" / "Song.chaosmaker", content, error, 48000.0));
    CHECK(content.warning.empty());
    const auto& clips = content.project.tracks()[0]->audioClips;
    CHECK(clips.size() == 4);
    for (const auto& clip : clips) {
        CHECK(clip.poolId.starts_with((dir.path / "Moved").string()));
        CHECK(content.audio.contains(clip.poolId) && content.audio.at(clip.poolId) != nullptr);
    }
    CHECK(content.audio.at(clips[1].poolId)->frames == 3000);
}

TEST(mediacollect, files_that_cannot_be_copied_are_named_and_kept_as_they_were)
{
    TempDir dir;
    const auto recorded = take(dir.path / "appdata" / "take.wav", 500);
    Project project;
    auto& track = project.addTrack(TrackKind::Audio, "Vox");
    addClip(project, track.id, recorded, 0);
    addClip(project, track.id, (dir.path / "missing.wav").string(), 1000);   // gone: left to the load

    // A file where the media folder should go: nothing can be copied.
    fs::create_directories(dir.path / "Song");
    { std::ofstream blocker(dir.path / "Song" / "Song Media"); blocker << "x"; }
    const auto collected = persistence::collectMedia(project, {recorded, (dir.path / "missing.wav").string()},
                                                     dir.path / "Song" / "Song.chaosmaker");
    CHECK(collected.paths.empty());
    CHECK(collected.problems.size() == 1 && collected.problems[0].find("take.wav") != std::string::npos);

    ProjectSession session;
    std::string error, warning;
    CHECK(session.saveProjectFile((dir.path / "Song" / "Song.chaosmaker").string(), project,
                                  {recorded}, {std::string()}, &error, &warning));
    CHECK(warning.find("could not be copied") != std::string::npos);
    CHECK(savedPaths(dir.path / "Song" / "Song.chaosmaker")[0] == recorded);    // still plays here
}
