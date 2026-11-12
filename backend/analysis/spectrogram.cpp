#include "analysis/spectrogram.h"

#include "dsp/fft.h"

#include <cmath>
#include <cstdint>
#include <complex>
#include <numbers>

namespace analysis {

int BandSpectrogram::bandAt(double hz) const noexcept
{
    for (int b = 0; b < bands; ++b)
        if (centreHz[b] >= hz) return b;
    return bands;
}

BandSpectrogram bandSpectrogram(const std::vector<float>& mono, double sampleRate, const Progress& progress)
{
    BandSpectrogram s;
    if (mono.empty() || !(sampleRate > 1000.0)) return s;
    s.sampleRate = sampleRate;
    s.hop = std::max(1, int(std::lround(sampleRate / 100.0)));
    int size = 2;
    while (size < int(sampleRate * 0.04)) size *= 2;          // ~43 ms: 2048 at 44.1 / 48 kHz
    const int bins = size / 2 + 1;

    // Bands: edges lo * 2^(i / 8). Each FFT bin adds its power to the band it falls in; a band too
    // narrow to hold a bin (the lowest ones) reads the bins either side of its centre instead.
    const double top = std::min(BandSpectrogram::HighestHz, 0.45 * sampleRate);
    const double octaves = std::log2(top / BandSpectrogram::LowestHz);
    s.bands = int(std::floor(octaves * BandSpectrogram::BandsPerOctave));
    struct Weight { int bin; float weight; };
    std::vector<std::vector<Weight>> weights(std::size_t(s.bands));
    s.centreHz.resize(std::size_t(s.bands));
    const double binHz = sampleRate / size;
    for (int b = 0; b < s.bands; ++b) {
        const double lo = BandSpectrogram::LowestHz * std::pow(2.0, double(b) / BandSpectrogram::BandsPerOctave);
        const double hi = BandSpectrogram::LowestHz * std::pow(2.0, double(b + 1) / BandSpectrogram::BandsPerOctave);
        s.centreHz[b] = float(std::sqrt(lo * hi));
        for (int k = int(std::ceil(lo / binHz)); k < bins && k * binHz < hi; ++k) weights[b].push_back({k, 1.0f});
        if (weights[b].empty()) {
            const double at = s.centreHz[b] / binHz;
            const int k = int(std::floor(at));
            const float frac = float(at - k);
            weights[b].push_back({std::clamp(k, 0, bins - 1), 1.0f - frac});
            weights[b].push_back({std::clamp(k + 1, 0, bins - 1), frac});
        }
        float sum = 0.0f;
        for (const auto& w : weights[b]) sum += w.weight;
        for (auto& w : weights[b]) w.weight /= std::max(sum, 1e-6f);   // mean power per bin
    }

    std::vector<float> window(static_cast<std::size_t>(size));
    for (int i = 0; i < size; ++i) window[i] = float(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / size));
    const Fft fft(size);
    std::vector<std::complex<double>> buffer(static_cast<std::size_t>(size));
    std::vector<float> power(static_cast<std::size_t>(bins));

    s.frames = int(mono.size() / std::size_t(s.hop)) + 1;
    s.magnitude.assign(std::size_t(s.frames) * s.bands, 0.0f);
    const auto n = std::int64_t(mono.size());
    for (int f = 0; f < s.frames; ++f) {
        if ((f & 255) == 0) {
            if (progress.cancelled()) return {};
            progress.at(double(f) / s.frames);
        }
        const std::int64_t start = std::int64_t(f) * s.hop - size / 2;
        for (int i = 0; i < size; ++i) {
            const auto at = start + i;
            buffer[i] = (at >= 0 && at < n) ? double(mono[std::size_t(at)] * window[i]) : 0.0;
        }
        fft.forward(buffer.data());
        for (int k = 0; k < bins; ++k) power[k] = float(std::norm(buffer[k]));
        float* row = &s.magnitude[std::size_t(f) * s.bands];
        for (int b = 0; b < s.bands; ++b) {
            float p = 0.0f;
            for (const auto& w : weights[b]) p += w.weight * power[w.bin];
            row[b] = std::sqrt(p) * (2.0f / float(size));
        }
    }
    progress.at(1.0);
    return s;
}

} // namespace analysis
