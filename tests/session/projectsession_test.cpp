#include "testkit.h"
#include "session/projectsession.h"

#include <filesystem>

TEST(projectsession, initial_state_and_paths)
{
    ProjectSession session;
    CHECK(session.currentFilePath().empty());
    CHECK(!session.isDirty());
    CHECK(session.restoreOnStartup());
    CHECK(session.sessionFilePath().empty());

    session.setCurrentFilePath("/path/to/project.chaos");
    CHECK(session.currentFilePath() == "/path/to/project.chaos");

    session.setRestoreOnStartup(false);
    CHECK(!session.restoreOnStartup());
}

TEST(projectsession, dirty_tracking_and_restoring_guard)
{
    ProjectSession session;
    CHECK(!session.isDirty());

    session.onDocumentModified(true);
    CHECK(session.isDirty());

    session.setDirty(false);
    CHECK(!session.isDirty());

    session.setRestoring(true);
    session.onDocumentModified(true);
    CHECK(!session.isDirty()); // guarded while restoring
}

TEST(projectsession, can_save_session_policy)
{
    ProjectSession session;
    // normal idle state
    CHECK(session.canSaveSession(false, false, false, false));

    // take busy
    CHECK(!session.canSaveSession(true, false, false, false));

    // project loading
    CHECK(!session.canSaveSession(false, true, false, false));

    // restoring
    CHECK(!session.canSaveSession(false, false, true, false));

    // save blocked
    CHECK(!session.canSaveSession(false, false, false, true));

    // restore on startup disabled
    session.setRestoreOnStartup(false);
    CHECK(!session.canSaveSession(false, false, false, false));
}

TEST(projectsession, save_and_load_candidate_roundtrip)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "daw_session_test";
    std::filesystem::create_directories(tempDir);
    const auto projFile = (tempDir / "test_session.chaos").string();
    const auto sessFile = (tempDir / "session.json").string();

    ProjectSession session;
    Project project;
    project.tempo().set(130.0, 3, 4, 48000.0);
    project.addTrack(TrackKind::Audio, "Rhythm");

    TrackList::MediaPaths paths;
    std::vector<std::string> effects{""};

    std::string err;
    session.setDirty(true);
    bool saved = session.saveProjectFile(projFile, project, paths, effects, &err);
    CHECK(saved);
    CHECK(err.empty());
    CHECK(session.currentFilePath() == projFile);
    CHECK(!session.isDirty());

    session.setSessionFilePath(sessFile);
    bool sessSaved = session.saveSession(project, paths, effects, &err);
    CHECK(sessSaved);

    ProjectSession::LoadCandidate candidate;
    bool prepared = session.prepareLoadCandidate(projFile, 48000.0, candidate, err);
    CHECK(prepared);
    CHECK(candidate.valid);
    CHECK(candidate.content.project.tracks().size() == 1);
    CHECK(candidate.content.project.tracks()[0]->name == "Rhythm");

    session.setDirty(true);
    session.commitLoadCandidate(candidate);
    CHECK(!session.isDirty());
    CHECK(session.currentFilePath() == projFile);

    auto newCand = session.prepareNewProjectCandidate(44100.0);
    CHECK(newCand.valid);
    CHECK(newCand.filePath.empty());
    CHECK(newCand.content.project.tracks().empty());
    CHECK_NEAR(newCand.content.project.tempo().bpm(), 120.0, 1e-6);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}
