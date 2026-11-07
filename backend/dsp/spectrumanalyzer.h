#pragma once

#include "dsp/fft.h"

#include <complex>
#include <span>
#include <vector>

// Turns a stream of mono samples into a spectrum for display. UI/worker thread only.
//
// The latest fftSize samples are windowed (Hann) and transformed. Each display frequency
// shows the loudest bin around it (or, where bins are wider than the display spacing, the
// value interpolated between the two nearest bins). Levels are dBFS: a full-scale sine
// reads about 0 dB. Rises show at once; falls are limited to fallDbPerSecond so the
// display does not flicker.
class SpectrumAnalyzer {
public:
    static constexpr double FloorDb = -120.0;

    explicit SpectrumAnalyzer(int fftSize = 4096);

    void setSampleRate(double sampleRate) noexcept;
    void setFallRate(double dbPerSecond) noexcept { m_fallDbPerSecond = dbPerSecond; }
    void clear();

    // Adds samples to the history (keeps the latest fftSize).
    void push(std::span<const float> samples);

    // Spectrum at the given frequencies; elapsedSeconds since the last update sets how far
    // levels may fall. The result stays valid until the next call.
    const std::vector<double>& update(std::span<const double> frequencies, double elapsedSeconds);

private:
    Fft m_fft;
    std::vector<float> m_history;     // circular, m_fft.size() samples
    std::size_t m_writePos = 0;
    std::vector<double> m_window;
    double m_windowSum = 1.0;
    std::vector<std::complex<double>> m_bins;
    std::vector<double> m_binDb;
    std::vector<double> m_display;
    double m_sampleRate = 48000.0;
    double m_fallDbPerSecond = 60.0;
};
