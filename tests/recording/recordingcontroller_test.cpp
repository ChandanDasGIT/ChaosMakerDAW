#include "testkit.h"
#include "recording/recordingcontroller.h"

#include <filesystem>

TEST(recordingcontroller, validate_eligibility_audio_disconnected)
{
    Project project;
    auto& t = project.addTrack(TrackKind::Audio, "Vocals", "Audio");
    t.recordArmed = true;

    std::vector<int> armed;
    std::string err;
    bool ok = RecordingController::validateEligibility(project, false, true, true, armed, err);
    CHECK(!ok);
    CHECK(err == "Recording needs a connected audio input and output.");
}

TEST(recordingcontroller, validate_eligibility_disarm_tracks_without_inputs)
{
    Project project;
    auto& t = project.addTrack(TrackKind::Instrument, "Synth", "Piano");
    t.recordArmed = true; // Piano does not have live input

    std::vector<int> armed;
    std::string err;
    bool ok = RecordingController::validateEligibility(project, true, true, true, armed, err);
    CHECK(!ok);
    CHECK(err == "Disarm tracks without audio inputs before recording.");
}

TEST(recordingcontroller, validate_eligibility_no_armed_tracks)
{
    Project project;
    project.addTrack(TrackKind::Audio, "Vocals", "Audio"); // not armed

    std::vector<int> armed;
    std::string err;
    bool ok = RecordingController::validateEligibility(project, true, true, true, armed, err);
    CHECK(!ok);
    CHECK(err == "Arm at least one Audio, Vocal, Acoustic Guitar or Electric Guitar track to record.");
}

TEST(recordingcontroller, validate_eligibility_success)
{
    Project project;
    auto& t1 = project.addTrack(TrackKind::Audio, "Vocals", "Audio");
    t1.recordArmed = true;
    auto& t2 = project.addTrack(TrackKind::Audio, "Guitar", "Acoustic Guitar");
    t2.recordArmed = true;

    std::vector<int> armed;
    std::string err;
    bool ok = RecordingController::validateEligibility(project, true, true, true, armed, err);
    CHECK(ok);
    CHECK(err.empty());
    CHECK(armed.size() == 2);
    CHECK(armed[0] == t1.id);
    CHECK(armed[1] == t2.id);
}

TEST(recordingcontroller, start_recording_lifecycle_and_modes)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "daw_test_recordings";
    std::filesystem::create_directories(tempDir);

    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);

    auto& tWet = project.addTrack(TrackKind::Audio, "Wet Track", "Audio");
    tWet.recordArmed = true;
    tWet.recordDry = false; // recordsWithFx == true

    auto& tDry = project.addTrack(TrackKind::Audio, "Dry Track", "Audio");
    tDry.recordArmed = true;
    tDry.recordDry = true; // recordsWithFx == false

    RecordingController controller;
    CHECK(!controller.isBusy());

    EngineCommand postedCommand;
    bool commandReceived = false;
    auto poster = [&](const EngineCommand& cmd) {
        postedCommand = cmd;
        commandReceived = true;
        return true;
    };

    std::vector<int> lockedTracks;
    auto locker = [&](const std::vector<int>& locked) {
        lockedTracks = locked;
    };

    std::string startMessage;
    bool started = controller.startRecording(
        project,
        tempDir.string(),
        48000.0,
        false, // countInEnabled
        120.0,
        4,
        4,
        [](int id) { return id == 1 ? 0 : 1; },
        poster,
        locker,
        startMessage);

    CHECK(started);
    CHECK(commandReceived);
    CHECK(postedCommand.type == EngineCommand::Type::StartRecordingGroup);
    CHECK(postedCommand.group != nullptr);
    CHECK(postedCommand.group->entries.size() == 2);

    CHECK(controller.isBusy());
    CHECK(controller.takeCount() == 2);
    CHECK(controller.primaryRecordingTrackId() == tWet.id);
    CHECK(lockedTracks.size() == 2);

    int attachedCount = 0;
    auto attacher = [&](int, std::int64_t, std::shared_ptr<const AudioData>, bool) {
        ++attachedCount;
    };

    bool unlocked = false;
    auto unlocker = [&]() {
        unlocked = true;
    };

    // Shutdown / finalize
    controller.shutdown(attacher, unlocker);

    CHECK(!controller.isBusy());
    CHECK(unlocked);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST(recordingcontroller, complete_take_empty_summary)
{
    RecordingController controller;
    auto result = controller.completeTake(nullptr, nullptr);
    CHECK(result.completedTracks == 0);
    CHECK(result.errors.empty());
}
