#pragma once
#include <cmath>
#include <cstdint>

// All time conversion happens here. One tempo for now. The interface already fits tempo changes.
class TempoMap {
public:
    void set(double bpm, int numerator, int denominator, double sampleRate) noexcept {
        m_bpm = bpm;
        m_numerator = numerator;
        m_denominator = denominator;
        m_sampleRate = sampleRate;
    }

    double beatAt(std::int64_t sample) const noexcept { return double(sample) * m_bpm / (60.0 * m_sampleRate); }
    std::int64_t sampleAt(double beat) const noexcept { return std::llround(beat * 60.0 * m_sampleRate / m_bpm); }
    double beatsPerBar() const noexcept { return m_numerator * 4.0 / m_denominator; }
    // Length of one counted beat (the meter's denominator note) in quarter-note beats.
    double beatUnit() const noexcept { return 4.0 / m_denominator; }

    double sampleRate() const noexcept { return m_sampleRate; }
    double bpm() const noexcept { return m_bpm; }
    int numerator() const noexcept { return m_numerator; }
    int denominator() const noexcept { return m_denominator; }

private:
    double m_bpm = 120.0;
    int m_numerator = 4;
    int m_denominator = 4;
    double m_sampleRate = 48000.0;
};
