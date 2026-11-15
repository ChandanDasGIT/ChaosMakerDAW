#pragma once
#include "instruments/harmonium/reedwave.h"

#include <array>
#include <cstdint>

// One key of the harmonium: every reed set (rank) of the current style, opened and closed by
// its stop while the key is held, behind one key valve. Real-time safe.
namespace harmonium {

constexpr int MaxRanks = 5;

struct Rank {
    reedwave::Timbre timbre = reedwave::Timbre::Reed;
    int octave = 0;                       // -1 bass / 16', +1 female / 4'
    double detuneCents = 0.0;             // the beating between sets
    float gain = 0.0f;                    // 0 when its stop is closed
};

// What all voices share for one short block: the bellows, already smoothed.
struct Air {
    float pressureFrom = 0.0f, pressureTo = 0.0f;   // ramps across the block
    double pitchRatio = 1.0;                        // pressure flattening, tremolo
};

class ReedVoice {
public:
    // inputKey is what note-off looks for; soundingKey has the scale changer applied.
    // A voice already sounding keeps its level, so a repeated key does not click.
    void start(int inputKey, int soundingKey, const std::array<Rank, MaxRanks>& ranks,
               double sampleRate, double attackSeconds) noexcept;
    void setRanks(const std::array<Rank, MaxRanks>& ranks) noexcept;   // stops, beating
    void release() noexcept;
    // Key up with sustain: full level for holdSeconds, then a fade to silence over fadeSeconds.
    void sustain(double holdSeconds, double fadeSeconds) noexcept;
    void cutTail() noexcept;              // a sustaining key ends in a short, click-free fade
    void fadeOut() noexcept;              // a few ms: style change, stolen voice
    void stop() noexcept;

    // Adds `frames` samples of this key to mono.
    void render(float* mono, int frames, const Air& air) noexcept;

    bool active() const noexcept { return m_stage != Stage::Off; }
    bool releasing() const noexcept
    {
        return m_stage == Stage::Release || m_stage == Stage::Fade || m_stage == Stage::Sustain;
    }
    bool sustaining() const noexcept { return m_stage == Stage::Sustain; }
    int inputKey() const noexcept { return m_inputKey; }
    int soundingKey() const noexcept { return m_soundingKey; }
    float level() const noexcept { return m_level; }
    // How much the reeds are still speaking up, 0..1: the breathy part of the attack.
    float breath() const noexcept { return m_stage == Stage::Attack ? 4.0f * m_level * (1.0f - m_level) : 0.0f; }
    int soundingRanks() const noexcept;
    double rankFrequency(int rank) const noexcept { return m_frequency[std::size_t(rank)]; }

private:
    enum class Stage { Off, Attack, Hold, Sustain, Release, Fade };
    void tune(const std::array<Rank, MaxRanks>& ranks) noexcept;

    Stage m_stage = Stage::Off;
    int m_inputKey = -1, m_soundingKey = -1;
    double m_sampleRate = 48000.0;
    std::array<reedwave::Oscillator, MaxRanks> m_osc{};
    std::array<double, MaxRanks> m_frequency{};
    std::array<float, MaxRanks> m_gain{}, m_gainTarget{};
    float m_level = 0.0f;
    float m_attackCoef = 0.0f, m_releaseCoef = 0.0f, m_fadeCoef = 0.0f, m_gainCoef = 0.0f;
    float m_sustainCoef = 1.0f;
    std::int64_t m_sustainHold = 0;       // frames left at full level
};

} // namespace harmonium
