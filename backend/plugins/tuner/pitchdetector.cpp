#include "pitchdetector.h"
#include <algorithm>
#include <cmath>
void PitchDetector::prepare(double rate) noexcept {
    rate = std::max(rate, 1000.0);
    m_decimation = std::max(1, int(rate / 12000.0));
    m_rate = rate / m_decimation;
    m_coefficient = 1.0f - std::exp(-6.28318530718f * float(std::min(1500.0, rate * 0.2) / rate));
    reset();
}
void PitchDetector::reset() noexcept {
    m_tau = 1; m_cumulative = 0;
    m_size = m_count = 0; m_filter = m_filter2 = m_frequency = 0;
    m_samples.fill(0); m_difference.fill(0);
}
bool PitchDetector::push(float sample) noexcept {
    m_filter += m_coefficient * (sample - m_filter);
    m_filter2 += m_coefficient * (m_filter - m_filter2);
    if (++m_count < m_decimation) return false;
    m_count = 0;
    if (m_size < int(m_samples.size())) {
        m_samples[m_size++] = m_filter2;
        return false;
    }
    // Spread YIN's work across incoming samples instead of a single audio block.
    if (!analyseStep()) return false;
    m_size = 0; m_tau = 1; m_cumulative = 0;
    return true;
}
bool PitchDetector::analyseStep() noexcept {
    const int maximum = std::min(256, int(m_rate / 55.0));
    const int minimum = std::max(2, int(m_rate / 1200.0));
    const int lag = m_tau++;
    double sum = 0;
    for (int j = 0; j < 768; ++j) {
        const double delta = m_samples[j] - m_samples[j + lag];
        sum += delta * delta;
    }
    m_cumulative += sum;
    m_difference[lag] = m_cumulative > 1e-15 ? float(sum * lag / m_cumulative) : 1.0f;
    if (lag < maximum) return false;
    m_difference[0] = 1;
    m_frequency = 0;
    double energy = 0;
    for (float x : m_samples) energy += x * x;
    if (energy / m_samples.size() < 1e-7) return true;
    for (int tau = minimum; tau < maximum; ++tau) {
        if (m_difference[tau] >= 0.15f) continue;
        while (tau + 1 < maximum && m_difference[tau + 1] < m_difference[tau]) ++tau;
        const float a = m_difference[tau-1], b = m_difference[tau], c = m_difference[tau+1];
        const float denominator = a - 2*b + c;
        const float correction = std::abs(denominator) > 1e-9f ? std::clamp(0.5f*(a-c)/denominator, -1.0f, 1.0f) : 0;
        m_frequency = float(m_rate / (tau + correction));
        return true;
    }
    return true;
}
