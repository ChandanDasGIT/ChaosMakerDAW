#include "recording/recordingcontroller.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <sstream>

RecordingController::RecordingController() = default;
RecordingController::~RecordingController() = default;

std::string RecordingController::generateTakeFilename(const std::string& folder, int trackId)
{
    static std::atomic<std::uint64_t> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto count = ++counter;

    std::ostringstream ss;
    ss << folder;
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') {
        ss << '/';
    }
    ss << "Take-" << trackId << "-" << now << "-" << count << ".wav";
    return ss.str();
}

std::vector<int> RecordingController::activeRecordingTrackIds() const
{
    std::vector<int> result;
    for (const auto& [id, take] : m_takes) {
        if (!take->released()) {
            result.push_back(id);
        }
    }
    for (const auto& [id, take] : m_midiTakes) {
        if (!take->isFinished()) {
            result.push_back(id);
        }
    }
    return result;
}

bool RecordingController::validateEligibility(
    const Project& project,
    bool audioReady,
    bool inputConnected,
    bool outputConnected,
    std::vector<int>& armedTracks,
    std::string& errorMessage)
{
    armedTracks.clear();
    errorMessage.clear();

    if (!audioReady || !outputConnected) {
        errorMessage = "Recording needs a connected audio input and output.";
        return false;
    }

    bool hasAudioTracks = false;
    for (const auto& track : project.tracks()) {
        if (!track->recordArmed) continue;
        if (track->hasLiveInput()) {
            armedTracks.push_back(track->id);
            hasAudioTracks = true;
        } else if (track->kind == TrackKind::Instrument) {
            armedTracks.push_back(track->id);
        } else {
            errorMessage = "Disarm tracks without audio inputs before recording.";
            return false;
        }
    }

    if (hasAudioTracks && !inputConnected) {
        errorMessage = "Recording needs a connected audio input and output.";
        return false;
    }

    if (armedTracks.empty()) {
        errorMessage = "Arm at least one Audio, Vocal, Acoustic Guitar or Electric Guitar track to record.";
        return false;
    }

    return true;
}

bool RecordingController::startRecording(
    const Project& project,
    const std::string& recordingsFolder,
    double sampleRate,
    bool countInEnabled,
    double bpm,
    int numerator,
    int denominator,
    const ChannelLookup& channelLookup,
    const CommandPoster& commandPoster,
    const LockTracksCallback& lockCallback,
    std::string& outMessage)
{
    std::vector<int> armedTracks;
    std::string err;
    if (!validateEligibility(project, true, true, true, armedTracks, err)) {
        outMessage = err;
        return false;
    }

    auto group = std::make_unique<RecordingGroup>();
    std::map<int, std::unique_ptr<TakeRecorder>> takes;
    std::map<int, std::unique_ptr<MidiTakeRecorder>> midiTakes;

    for (int id : armedTracks) {
        const auto* track = project.findTrack(id);
        if (!track) continue;

        if (track->hasLiveInput()) {
            const auto filePath = generateTakeFilename(recordingsFolder, id);
            const auto source = track->recordsWithFx()
                ? TakeRecorder::Source::PostFx
                : TakeRecorder::Source::DryInput;

            auto take = std::make_unique<TakeRecorder>(filePath, sampleRate, source);
            const int channel = channelLookup ? channelLookup(id) : 0;
            group->entries.push_back({id, channel, take.get(), nullptr, true});
            takes.emplace(id, std::move(take));
        } else if (track->kind == TrackKind::Instrument) {
            auto midiTake = std::make_unique<MidiTakeRecorder>(id);
            group->entries.push_back({id, -1, nullptr, midiTake.get(), true});
            midiTakes.emplace(id, std::move(midiTake));
        }
    }

    EngineCommand command;
    command.type = EngineCommand::Type::StartRecordingGroup;
    command.group = group.get();

    if (countInEnabled) {
        const double frames = numerator * 4.0 / denominator * 60.0 / bpm * sampleRate;
        if (!std::isfinite(frames) || frames < 1.0 || frames >= double(INT64_MAX / 4)) {
            outMessage = "Count-in duration is outside the supported range.";
            return false;
        }
        command.a = std::llround(frames);
    }

    if (!commandPoster || !commandPoster(command)) {
        outMessage = "Could not start recording; audio queue is full.";
        return false;
    }

    m_takes = std::move(takes);
    m_midiTakes = std::move(midiTakes);
    m_tempo.set(bpm, numerator, denominator, sampleRate);
    m_group = std::move(group);
    m_countInMessagePending = countInEnabled;
    m_recordTrack = armedTracks.front();

    if (lockCallback) {
        lockCallback(armedTracks);
    }

    if (countInEnabled) {
        outMessage = "Count-in / pre-roll · one bar before recording…";
    } else if (armedTracks.size() == 1) {
        const auto* track = project.findTrack(m_recordTrack);
        if (track && track->kind == TrackKind::Instrument && !track->hasLiveInput()) {
            outMessage = "Recording MIDI for " + track->name + "…";
        } else if (track && track->recordsWithFx()) {
            outMessage = "Recording " + track->name + " with FX…";
        } else {
            outMessage = "Recording dry input · playback FX stay editable…";
        }
    } else {
        outMessage = "Recording " + std::to_string(armedTracks.size()) + " tracks with independent inputs and FX…";
    }

    return true;
}

bool RecordingController::poll(
    bool isEngineRecording,
    std::int64_t engineRecordingStart,
    const TempoMap& tempo,
    const AttachTakeCallback& attachCallback,
    const AttachMidiTakeCallback& attachMidiCallback)
{
    m_tempo = tempo;
    if (isEngineRecording) {
        m_recordStartBeat = tempo.beatAt(engineRecordingStart);
    }

    for (const auto& [id, take] : m_takes) {
        if (take->released() && take->done() && !m_attachedTakes.contains(id)) {
            const auto audio = take->audio();
            if (audio && audio->frames > 0 && attachCallback) {
                attachCallback(id, take->startSample - m_compensation, audio, take->source() == TakeRecorder::Source::PostFx);
            }
            m_attachedTakes.insert(id);
        }
    }

    for (const auto& [id, take] : m_midiTakes) {
        if (take->isFinished() && !m_attachedMidiTakes.contains(id)) {
            const auto lengthSamples = take->endSample() - take->startSample();
            if (lengthSamples > 0 && !take->notes().empty() && attachMidiCallback) {
                std::vector<MidiNote> notes;
                notes.reserve(take->notes().size());
                int noteId = 1;
                for (const auto& c : take->notes()) {
                    const double startBeat = m_tempo.beatAt(c.on - take->startSample());
                    const double lengthBeats = std::max(0.01, m_tempo.beatAt(c.off) - m_tempo.beatAt(c.on));
                    notes.push_back(MidiNote{
                        noteId++,
                        c.key,
                        startBeat,
                        lengthBeats,
                        c.velocity,
                        false
                    });
                }
                attachMidiCallback(id, take->startSample(), lengthSamples, std::move(notes));
            }
            m_attachedMidiTakes.insert(id);
        }
    }

    const bool audioDone = std::all_of(m_takes.begin(), m_takes.end(), [](const auto& pair) { return pair.second->done(); });
    const bool midiDone = std::all_of(m_midiTakes.begin(), m_midiTakes.end(), [](const auto& pair) { return pair.second->isFinished(); });

    return (m_group && m_group->released.load(std::memory_order_acquire) && audioDone && midiDone);
}

RecordingController::FinalizeResult RecordingController::completeTake(
    const AttachTakeCallback& attachCallback,
    const AttachMidiTakeCallback& attachMidiCallback,
    const UnlockCallback& unlockCallback)
{
    FinalizeResult result;
    if (!isBusy()) return result;

    if (m_group && m_group->inputLost.load(std::memory_order_relaxed)) {
        result.errors.push_back("Recording stopped: an audio input became unavailable; completed samples were kept.");
    }

    if (unlockCallback) {
        unlockCallback();
    }

    for (const auto& [id, take] : m_takes) {
        const auto audio = take->audio();
        if (!take->error().empty()) {
            result.errors.push_back(take->error());
        }
        if (audio && audio->frames > 0) {
            if (!m_attachedTakes.contains(id)) {
                if (attachCallback) {
                    attachCallback(id, take->startSample - m_compensation, audio, take->source() == TakeRecorder::Source::PostFx);
                }
                m_attachedTakes.insert(id);
            }
            ++result.completedTracks;
        }
    }

    for (const auto& [id, take] : m_midiTakes) {
        const auto lengthSamples = take->endSample() - take->startSample();
        if (lengthSamples > 0 && !take->notes().empty()) {
            if (!m_attachedMidiTakes.contains(id)) {
                if (attachMidiCallback) {
                    std::vector<MidiNote> notes;
                    notes.reserve(take->notes().size());
                    int noteId = 1;
                    for (const auto& c : take->notes()) {
                        const double startBeat = m_tempo.beatAt(c.on - take->startSample());
                        const double lengthBeats = std::max(0.01, m_tempo.beatAt(c.off) - m_tempo.beatAt(c.on));
                        notes.push_back(MidiNote{
                            noteId++,
                            c.key,
                            startBeat,
                            lengthBeats,
                            c.velocity,
                            false
                        });
                    }
                    attachMidiCallback(id, take->startSample(), lengthSamples, std::move(notes));
                }
                m_attachedMidiTakes.insert(id);
            }
            ++result.completedTracks;
        }
    }

    if (result.errors.empty() && m_group && m_group->captureFailed.load(std::memory_order_relaxed)) {
        result.errors.push_back("Recording stopped because audio capture could not continue; completed samples were kept.");
    }

    m_group.reset();
    m_takes.clear();
    m_midiTakes.clear();
    m_attachedTakes.clear();
    m_attachedMidiTakes.clear();
    m_recordTrack = -1;
    m_countInMessagePending = false;

    if (!result.errors.empty()) {
        std::string fullError;
        for (std::size_t i = 0; i < result.errors.size(); ++i) {
            if (i > 0) fullError += " · ";
            fullError += result.errors[i];
        }
        result.message = fullError;
    } else if (result.completedTracks > 0) {
        result.message = "Recorded " + std::to_string(result.completedTracks) + " track(s).";
    } else {
        result.message = "Take ended before any audio was captured.";
    }

    return result;
}

void RecordingController::shutdown(
    const AttachTakeCallback& attachCallback,
    const AttachMidiTakeCallback& attachMidiCallback,
    const UnlockCallback& unlockCallback)
{
    if (!isBusy()) return;
    for (auto& [id, take] : m_takes) {
        take->finish();
        take->wait();
    }
    for (auto& [id, take] : m_midiTakes) {
        if (!take->isFinished()) {
            take->finish(take->startSample());
        }
    }
    completeTake(attachCallback, attachMidiCallback, unlockCallback);
}

std::vector<std::pair<float, float>> RecordingController::livePeaks(int trackId, int bins, double from, double to) const
{
    const auto it = m_takes.find(trackId);
    if (it == m_takes.end() || !it->second) return {};
    return it->second->livePeaks().summary(bins, from, to);
}
