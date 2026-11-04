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

TEST(recordingcontroller, validate_eligibility_instrument_armed_is_eligible)
{
    Project project;
    auto& t = project.addTrack(TrackKind::Instrument, "Synth", "Piano");
    t.recordArmed = true;

    std::vector<int> armed;
    std::string err;
    bool ok = RecordingController::validateEligibility(project, true, false, true, armed, err);
    CHECK(ok);
    CHECK(err.empty());
    CHECK(armed.size() == 1);
    CHECK(armed[0] == t.id);
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

TEST(recordingcontroller, start_recording_midi_take_lifecycle)
{
    Project project;
    project.tempo().set(120.0, 4, 4, 48000.0);
    auto& synth = project.addTrack(TrackKind::Instrument, "Synth", "Piano");
    synth.recordArmed = true;

    RecordingController controller;
    EngineCommand postedCommand;
    auto poster = [&](const EngineCommand& cmd) {
        postedCommand = cmd;
        return true;
    };

    std::vector<int> locked;
    auto locker = [&](const std::vector<int>& ids) {
        locked = ids;
    };

    std::string msg;
    bool started = controller.startRecording(
        project,
        "/tmp",
        48000.0,
        false,
        120.0,
        4,
        4,
        nullptr,
        poster,
        locker,
        msg);

    CHECK(started);
    CHECK(postedCommand.type == EngineCommand::Type::StartRecordingGroup);
    CHECK(postedCommand.group != nullptr);
    CHECK(postedCommand.group->entries.size() == 1);
    CHECK(postedCommand.group->entries[0].recorder == nullptr);
    CHECK(postedCommand.group->entries[0].midiRecorder != nullptr);
    CHECK(controller.isBusy());
    CHECK(controller.takeCount() == 1);
    CHECK(controller.primaryRecordingTrackId() == synth.id);
    CHECK(locked.size() == 1 && locked[0] == synth.id);

    auto* midiRec = postedCommand.group->entries[0].midiRecorder;
    midiRec->begin(0);
    // Simulate playing middle C for 0.5s (24000 samples @ 48kHz = 1.0 beat at 120 bpm)
    midiRec->capture(MidiEvent{0, 0x90, 60, 100}, 0);
    midiRec->capture(MidiEvent{24000, 0x80, 60, 0}, 0);
    midiRec->finish(48000);

    int attachedMidiCount = 0;
    int receivedTrackId = -1;
    std::int64_t receivedStart = -1;
    std::int64_t receivedLength = -1;
    std::vector<MidiNote> receivedNotes;

    auto midiAttacher = [&](int trackId, std::int64_t start, std::int64_t length, std::vector<MidiNote> notes) {
        ++attachedMidiCount;
        receivedTrackId = trackId;
        receivedStart = start;
        receivedLength = length;
        receivedNotes = std::move(notes);
    };

    bool unlocked = false;
    auto unlocker = [&]() { unlocked = true; };

    auto result = controller.completeTake(nullptr, midiAttacher, unlocker);
    CHECK(!controller.isBusy());
    CHECK(unlocked);
    CHECK(result.completedTracks == 1);
    CHECK(attachedMidiCount == 1);
    CHECK(receivedTrackId == synth.id);
    CHECK(receivedStart == 0);
    CHECK(receivedLength == 48000);
    CHECK(receivedNotes.size() == 1);
    CHECK(receivedNotes[0].note == 60);
    CHECK_NEAR(receivedNotes[0].startBeat, 0.0, 1e-4);
    CHECK_NEAR(receivedNotes[0].lengthBeats, 1.0, 1e-4);
    CHECK(receivedNotes[0].velocity == 100);
}
