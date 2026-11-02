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
    bool recording() const { return m_reportedRecording.load(std::memory_order_relaxed); }
    TakeRecorder* releasedRecorder() const { return m_releasedRecorder.load(std::memory_order_acquire); }
    void clearReleasedRecorder() { m_releasedRecorder.store(nullptr, std::memory_order_relaxed); }
    DeviceLatency latency() const;
    double sampleRate() const;
    bool setBufferSize(int frames);
    std::string lastError() const;
    std::vector<std::string> inputNames() const;
    std::vector<std::string> outputNames() const;
    bool connectInput(const std::string& source);
    bool connectOutput(const std::string& left, const std::string& right);
    void setMonitorEqBand(int band, float gainDb) noexcept { m_monitorEq.setBandGain(band, gainDb); }

    // Offline rendering uses the same path as the real-time one.
    void renderBlock(const AudioIO& io) noexcept;

    // AudioCallback, audio thread
    void process(const AudioIO& io) noexcept override;
    void deviceChanged(double sampleRate, int maxFrames) override;
    void xrunOccurred() noexcept override;

private:
    std::unique_ptr<AudioDevice> m_device;
    SnapshotSlot<EngineSnapshot> m_snapshot;
    SnapshotSlot<EngineSnapshot> m_playback;
    void finishRecording() noexcept;
    void mixPlayback(const AudioIO& io, const EngineSnapshot* snapshot, int frames, TrackMeters* liveMeters) noexcept;
    TakeRecorder* m_recorder = nullptr;
    std::atomic<TakeRecorder*> m_releasedRecorder{nullptr};
    std::atomic<std::int64_t> m_reportedPosition{0}, m_recordingStart{0};
    std::atomic<bool> m_reportedPlaying{false}, m_reportedRecording{false};
    std::int64_t m_position = 0, m_loopStart = 0, m_loopEnd = 0;
    bool m_playing = false, m_looping = false;
    RtQueue<EngineCommand, 512> m_commands;
    RtQueue<EngineEvent, 512> m_events;
    TenBandEq m_monitorEq;
    // Fixed scratch space keeps FX independent of speaker routing and recording allocation-free.
    std::array<std::array<float, 1024>, 2> m_fxAudio{};
    int m_fxBlockFrames = 1024;
    std::array<std::array<float,1024>,2> m_playbackAudio{};
    std::array<std::array<float,1024>,2> m_printedAudio{}, m_liveAudio{};
    bool m_resetPlayback = false;
    std::atomic<float> m_inputTrimGain{1.0f};
    Smoother m_inputTrim;
    PeakMeter m_rawMeter, m_preFxMeter, m_leftMeter, m_rightMeter;
    bool m_cleanMonitorEnabled = false;                   // audio thread only
    float m_cleanMonitorGain = 1.0f;                      // audio thread only
    std::atomic<double> m_tempoBpm{120.0};                  // UI writes, audio reads once per block
    double m_sampleRate = 48000.0;                        // audio thread only
};
