#pragma once
#include "audiodevice/audiodevice.h"
#include "engine/enginecommands.h"
#include "engine/enginesnapshot.h"
#include "plugins/eq/tenbandeq.h"
#include "dsp/peakmeter.h"
#include "processor/smoother.h"
#include "rt/rtqueue.h"
#include "rt/rtsafe.h"
#include "rt/snapshotslot.h"

#include <memory>
#include <atomic>
#include <array>
#include "recording/takerecorder.h"
#include "recording/recordinggroup.h"

class AudioEngine final : public AudioCallback {
public:
    explicit AudioEngine(std::unique_ptr<AudioDevice> device);
    ~AudioEngine() override;

    // UI thread
    bool start(int wantedBufferFrames);
    bool setTempoBpm(double bpm) noexcept;
    bool setInputTrimDb(double db) noexcept;
    struct MonitorLevels {
        float raw = 0, preFx = 0, left = 0, right = 0;
        bool rawClip = false, preFxClip = false, leftClip = false, rightClip = false;
    };
    MonitorLevels takeMonitorLevels() noexcept;
    void clearMonitorClips() noexcept;
    void stop();
    bool post(const EngineCommand& command) noexcept { return m_commands.push(command); }
    // A note from the typing keyboard (any thread but the audio one). It joins the next block's
    // MIDI input, so it reaches the same tracks as a MIDI keyboard (MON or REC) and is recorded
    // the same way. False when the queue is full.
    bool sendKeyboardMidi(const MidiEvent& event) noexcept { return m_keyboardMidi.push(event); }
    bool nextEvent(EngineEvent& event) noexcept { return m_events.pop(event); }
    void publish(std::unique_ptr<EngineSnapshot> snapshot);   // also frees retired snapshots
    bool tryPublish(std::unique_ptr<EngineSnapshot>& snapshot);
    void collectGarbage();
    void publishPlayback(std::unique_ptr<EngineSnapshot> snapshot);
    void publishMonitor(std::unique_ptr<EngineSnapshot> snapshot);
    bool tryPublishPlayback(std::unique_ptr<EngineSnapshot>& snapshot);
    std::int64_t position() const { return m_reportedPosition.load(std::memory_order_relaxed); }
    std::int64_t recordingStart() const { return m_recordingStart.load(std::memory_order_relaxed); }
    bool playing() const { return m_reportedPlaying.load(std::memory_order_relaxed); }
    std::int64_t countInSamplesRemaining() const { return m_countInRemaining.load(std::memory_order_relaxed); }
    bool recording() const { return m_reportedRecording.load(std::memory_order_relaxed); }
    TakeRecorder* releasedRecorder() const { return m_releasedRecorder.load(std::memory_order_acquire); }
    void clearReleasedRecorder() { m_releasedRecorder.store(nullptr, std::memory_order_relaxed); }
    DeviceLatency latency() const;
    double sampleRate() const;
    // Audio dropouts reported by the device since start or the last reset. The device may report
    // them from any thread, so this is a plain atomic counter.
    std::uint64_t xrunCount() const noexcept { return m_xruns.load(std::memory_order_relaxed); }
    void resetXruns() noexcept { m_xruns.store(0, std::memory_order_relaxed); }
    bool setBufferSize(int frames);
    std::string lastError() const;
    std::vector<std::string> inputNames() const;
    std::vector<std::string> outputNames() const;
    bool connectInput(const std::string& source);
    int connectInputChannel(const std::string& source);
    int inputChannelCapacity() const { return m_device ? m_device->inputChannelCapacity() : 0; }
    bool connectOutput(const std::string& left, const std::string& right);
    std::vector<std::string> midiInputNames() const;
    void refreshMidiInputs();
    bool connectMidiInput(const std::string& source);
    std::string connectedMidiInput() const;
    void setMonitorEqBand(int band, float gainDb) noexcept { m_monitorEq.setBandGain(band, gainDb); }
    float masterVolume() const noexcept { return m_masterVolume.load(std::memory_order_relaxed); }
    void setMasterVolume(float volume) noexcept { m_masterVolume.store(std::clamp(volume, 0.0f, 2.0f), std::memory_order_relaxed); }
    bool masterMuted() const noexcept { return m_masterMuted.load(std::memory_order_relaxed); }
    void setMasterMuted(bool muted) noexcept { m_masterMuted.store(muted, std::memory_order_relaxed); }

    // Offline rendering uses the same path as the real-time one.
    void renderBlock(const AudioIO& io) noexcept;

    // AudioCallback, audio thread
    void process(const AudioIO& io) noexcept override;
    void deviceChanged(double sampleRate, int maxFrames) override;
    void xrunOccurred() noexcept override;

private:
    void processInput(const AudioIO& io) noexcept;          // process() with the MIDI input merged
    std::unique_ptr<AudioDevice> m_device;
    SnapshotSlot<EngineSnapshot> m_snapshot;
    SnapshotSlot<EngineSnapshot> m_playback;
    void finishRecording() noexcept;
    void mixMetronome(const AudioIO& io, int offset, int frames, const TempoMap& tempo) noexcept;
    void renderTracks(const AudioIO& io, const EngineSnapshot& snapshot, int frames) noexcept;
    RecordingGroup* m_recordingGroup = nullptr;
    void mixPlayback(const AudioIO& io, const EngineSnapshot* snapshot, int frames, TrackMeters* liveMeters) noexcept;
    void populateTrackMidi(const TrackRender& track, std::int64_t position, int count, bool loopWrapped, bool includeLiveMidi = true, const MidiBuffer* hwMidi = nullptr, bool routeHwMidi = false) noexcept;
    static bool trackReceivesHwMidi(const TrackRender& track, const EngineSnapshot* snapshot) noexcept;
    TakeRecorder* m_recorder = nullptr;
    std::atomic<TakeRecorder*> m_releasedRecorder{nullptr};
    std::atomic<std::int64_t> m_reportedPosition{0}, m_recordingStart{0}, m_countInRemaining{0};
    std::atomic<bool> m_reportedPlaying{false}, m_reportedRecording{false};
    std::int64_t m_position = 0, m_loopStart = 0, m_loopEnd = 0;
    std::int64_t m_countInTarget = -1;
    bool m_playing = false, m_looping = false;
    bool m_metronomeEnabled = false;
    bool m_stopAtEnd = true; // Stop playback at the end of the arrangement (user setting).
    float m_metronomeVolume = 0.5f;
    RtQueue<EngineCommand, 512> m_commands;
    RtQueue<MidiEvent, 256> m_keyboardMidi;
    MidiBuffer m_inputMidi;                                 // device MIDI + typing keyboard, one block
    RtQueue<EngineEvent, 512> m_events;
    std::atomic<std::uint64_t> m_xruns{0};
    TenBandEq m_monitorEq;
    // Fixed scratch space keeps FX independent of speaker routing and recording allocation-free.
    std::array<std::array<float, 1024>, 2> m_fxAudio{};
    int m_fxBlockFrames = 1024;
    std::array<std::array<float,1024>,2> m_playbackAudio{};
    std::array<std::array<float,1024>,2> m_printedAudio{}, m_liveAudio{};
    MidiBuffer m_trackMidi;
    struct LiveMidiMessage {
        int trackId = -1;
        MidiEvent event;
    };
    struct LiveTrackAudition {
        int trackId = -1;
        int activeNotes = 0;
        int tailSamples = 0;
    };
    std::array<LiveMidiMessage, 64> m_liveMidiBatch{};
    int m_liveMidiCount = 0;
    std::array<LiveTrackAudition, 16> m_liveAuditions{};

    LiveTrackAudition* findOrCreateAudition(int trackId) noexcept;
    LiveTrackAudition* findAudition(int trackId) noexcept;
    bool isAuditionActive(int trackId) const noexcept;
    bool anyAuditionActive() const noexcept;

    bool m_resetPlayback = false;
    std::atomic<float> m_inputTrimGain{1.0f};
    std::atomic<float> m_masterVolume{1.0f};
    std::atomic<bool> m_masterMuted{false};
    Smoother m_inputTrim;
    PeakMeter m_rawMeter, m_preFxMeter, m_leftMeter, m_rightMeter;
    bool m_cleanMonitorEnabled = false;                   // audio thread only
    float m_cleanMonitorGain = 1.0f;                      // audio thread only
    std::atomic<double> m_tempoBpm{120.0};                  // UI writes, audio reads once per block
    double m_sampleRate = 48000.0;                        // audio thread only
};
