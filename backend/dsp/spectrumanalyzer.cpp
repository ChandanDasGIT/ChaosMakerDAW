#include "spectrumanalyzer.h"

#include <algorithm>
#include <cmath>

SpectrumAnalyzer::SpectrumAnalyzer(int fftSize)
    : m_fft(fftSize)
{
    const auto n = std::size_t(m_fft.size());
    m_history.assign(n, 0.0f);
    m_bins.resize(n);
    m_binDb.assign(n / 2 + 1, FloorDb);
    m_window.resize(n);
    constexpr double twoPi = 6.283185307179586476925;
    m_windowSum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        m_window[i] = 0.5 - 0.5 * std::cos(twoPi * double(i) / double(n));
        m_windowSum += m_window[i];
    }
}

void SpectrumAnalyzer::setSampleRate(double sampleRate) noexcept
{
    if (std::isfinite(sampleRate) && sampleRate > 1000.0) m_sampleRate = sampleRate;
}

void SpectrumAnalyzer::clear()
{
    std::fill(m_history.begin(), m_history.end(), 0.0f);
    m_writePos = 0;
    std::fill(m_display.begin(), m_display.end(), FloorDb);
}

void SpectrumAnalyzer::push(std::span<const float> samples)
{
    const std::size_t n = m_history.size();
    // Only the newest n samples matter.
    if (samples.size() > n) samples = samples.subspan(samples.size() - n);
    for (float s : samples) {
        m_history[m_writePos] = std::isfinite(s) ? s : 0.0f;
        m_writePos = (m_writePos + 1) % n;
    }
}

const std::vector<double>& SpectrumAnalyzer::update(std::span<const double> frequencies, double elapsedSeconds)
{
    const std::size_t n = m_history.size();
    for (std::size_t i = 0; i < n; ++i)
        m_bins[i] = {double(m_history[(m_writePos + i) % n]) * m_window[i], 0.0};
    m_fft.forward(m_bins.data());

    // Amplitude of a sine = 2 |X| / sum(window).
    const double scale = 2.0 / m_windowSum;
    for (std::size_t k = 0; k < m_binDb.size(); ++k) {
        const double amplitude = std::abs(m_bins[k]) * scale;
        m_binDb[k] = std::max(FloorDb, 20.0 * std::log10(std::max(amplitude, 1e-12)));
    }

    const double binHz = m_sampleRate / double(n);
    const auto lastBin = double(m_binDb.size() - 1);
    const auto valueAt = [&](double hz) {
        const double pos = std::clamp(hz / binHz, 0.0, lastBin);
        const auto below = std::size_t(pos);
        const auto above = std::min(below + 1, m_binDb.size() - 1);
        const double t = pos - double(below);
        return m_binDb[below] + t * (m_binDb[above] - m_binDb[below]);
    };

    if (m_display.size() != frequencies.size()) m_display.assign(frequencies.size(), FloorDb);
    const double fall = std::max(0.0, elapsedSeconds) * m_fallDbPerSecond;
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        // Range covered by this display point: halfway (on a log scale) to its neighbours.
        const double hz = frequencies[i];
        const double lo = i > 0 ? std::sqrt(frequencies[i - 1] * hz) : hz;
        const double hi = i + 1 < frequencies.size() ? std::sqrt(frequencies[i + 1] * hz) : hz;
        const auto first = std::size_t(std::ceil(lo / binHz));
        const auto last = std::min(std::size_t(std::floor(hi / binHz)), m_binDb.size() - 1);
        double level = valueAt(hz);
        for (std::size_t k = first; k <= last && first <= last; ++k) level = std::max(level, m_binDb[k]);
        if (!(hz > 0.0) || hz >= m_sampleRate * 0.5) level = FloorDb;
        m_display[i] = std::max(level, m_display[i] - fall);
    }
    return m_display;
}
