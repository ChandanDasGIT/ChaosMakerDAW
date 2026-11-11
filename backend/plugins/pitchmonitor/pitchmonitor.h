#pragma once

#include "dsp/pitchtracker.h"
#include "plugins/mixfx/mixeffect.h"
#include "rt/rtqueue.h"

#include <atomic>
#include <cstdint>
#include <vector>

// Pitch Monitor (restMap G18): the sound passes through unchanged; while its page listens, the
// pitch of what goes through (mixed to mono) is followed every 10 ms and each reading is queued
// for the page with its song position. The key it is measured against is in its settings (the
// "key" and "scale" choices); the page does the measuring.
class PitchMonitor final : public MixEffect {
public:
    struct Reading {
        std::int64_t songSample = 0;      // where in the song the frame's centre was played
        float hz = 0.0f;                  // 0: no pitch
        float clarity = 0.0f;
        float levelDb = -120.0f;
        bool playing = false;             // the transport was running (else songSample stands still)
    };
    static constexpr std::size_t QueueSize = 512;   // ~5 s of readings

    PitchMonitor();

    // The page (any thread). Not listening, nothing is measured or queued.
    void setListening(bool listening) noexcept { m_listening.store(listening, std::memory_order_relaxed); }
    bool listening() const noexcept { return m_listening.load(std::memory_order_relaxed); }
    bool read(Reading& reading) noexcept { return m_readings.pop(reading); }

protected:
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    pitch::Tracker m_tracker;
    std::vector<float> m_mono;
    std::int64_t m_pushed = 0;            // samples given to the tracker since its reset
    bool m_wasListening = false;
    std::atomic<bool> m_listening{false};
    RtQueue<Reading, QueueSize> m_readings;
};
