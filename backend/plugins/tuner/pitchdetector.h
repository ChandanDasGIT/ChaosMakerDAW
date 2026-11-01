#pragma once
#include <array>

// YIN analysis at a decimated rate near 12 kHz. All storage is fixed-size.
class PitchDetector {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    bool push(float sample) noexcept; // true when a new estimate is available
    float frequency() const noexcept { return m_frequency; }
private:
    bool analyseStep() noexcept;
    int m_tau = 1;
    double m_cumulative = 0;
    std::array<float, 1024> m_samples{};
    std::array<float, 257> m_difference{};
    int m_size = 0, m_decimation = 4, m_count = 0;
    float m_filter = 0, m_filter2 = 0, m_coefficient = 0.2f, m_frequency = 0;
    double m_rate = 12000;
};
