#pragma once

#include <array>
#include <cstddef>
#include <vector>

// The tone of one reed, stored as one period in a table and read back at any pitch.
//
// Each timbre has a table per harmonic count (1, 2, 3, 4, 6, ... 256 harmonics) so a note never
// holds a harmonic above the limit it was made for: nothing folds back past Nyquist. Each
// timbre also comes in two pressures: a soft table (little air, few overtones) and a full one;
// the oscillator blends them by the bellows pressure.
//
// The tables are built once, on first use, off the audio thread (Harmonium's constructor), and
// never change after that, so any thread may read them.
namespace reedwave {

enum class Timbre {
    Reed,        // Indian brass reed: a puff of air per swing; overtones peak near the 3rd, then fall
    Diapason,    // Western reed organ 8': full, round
    Dulciana,    // Western reed organ 8': soft, few overtones
    Bourdon,     // Western reed organ 16': strong fundamental, weak even harmonics
};
constexpr int TimbreCount = 4;

constexpr int TableSize = 4096;                              // one period; plus a guard sample
constexpr std::array<int, 16> HarmonicCounts{1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256};
constexpr double TopHarmonicLimit = 0.42;                    // highest harmonic stays below 0.42 x rate

// Amplitude of harmonic h (1-based) for a timbre, soft (pressure 0) or full (pressure 1).
// Not normalised; the tables are.
double harmonicAmplitude(Timbre timbre, bool full, int harmonic) noexcept;

class Tables {
public:
    static const Tables& instance();                         // builds on the first call

    // TableSize + 1 samples (the last repeats the first, for interpolation).
    const float* table(Timbre timbre, bool full, int level) const noexcept;

    // The richest level whose harmonics all stay below TopHarmonicLimit x sampleRate.
    static int levelFor(double frequencyHz, double sampleRate) noexcept;

private:
    Tables();
    std::vector<float> m_data;                               // [timbre][full][level][sample]
};

// Reads one reed at a fixed pitch. Real-time safe.
class Oscillator {
public:
    // Picks the tables for this pitch. phase is 0..1, the start point in the period.
    void start(Timbre timbre, double frequencyHz, double sampleRate, double phase = 0.0) noexcept;

    // Changes the pitch a little (bellows pressure flattens a reed) without changing tables.
    void setFrequency(double frequencyHz, double sampleRate) noexcept;

    // One sample; pressure 0..1 blends the soft table into the full one.
    float next(float pressure) noexcept
    {
        const double position = m_phase * TableSize;
        const auto index = std::size_t(position);
        const float t = float(position - double(index));
        const float soft = m_soft[index] + (m_soft[index + 1] - m_soft[index]) * t;
        const float full = m_full[index] + (m_full[index + 1] - m_full[index]) * t;
        m_phase += m_increment;
        if (m_phase >= 1.0) m_phase -= 1.0;
        return soft + (full - soft) * pressure;
    }

    double increment() const noexcept { return m_increment; }
    int level() const noexcept { return m_level; }

private:
    const float* m_soft = nullptr;
    const float* m_full = nullptr;
    double m_phase = 0.0, m_increment = 0.0;
    int m_level = 0;
};

} // namespace reedwave
