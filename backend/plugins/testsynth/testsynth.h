#pragma once
#include "processor/processor.h"

#include <array>
#include <cmath>
#include <cstdint>

class TestSynth final : public Processor {
public:
    TestSynth();
    ~TestSynth() override = default;

    std::string typeId() const override { return "testsynth"; }

    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    struct Voice {
        bool active = false;
        int key = -1;
        float targetGain = 0.0f;
        float currentLevel = 0.0f;
        double phase = 0.0;
        double phaseInc = 0.0;
        enum class State { Off, Attack, Sustain, Release } state = State::Off;
        std::uint64_t age = 0;
    };

    std::span<const Voice> voices() const noexcept { return m_voices; }

private:
    Voice& allocateVoice() noexcept;
    void noteOn(int key, int velocity) noexcept;
    void noteOff(int key) noexcept;
    void allNotesOff() noexcept;

    double m_sampleRate = 48000.0;
    float m_attackRate = 0.01f;
    float m_releaseRate = 0.001f;
    std::uint64_t m_ageCounter = 0;
    std::array<Voice, 16> m_voices{};
};
