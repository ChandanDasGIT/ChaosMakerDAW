#include "analysis/hpss.h"

#include <algorithm>
#include <cstddef>

namespace analysis {
namespace {

float medianOf(std::vector<float>& values)
{
    const auto mid = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

} // namespace

Separation separate(const BandSpectrogram& s, const Progress& progress)
{
    Separation out;
    if (s.frames <= 0 || s.bands <= 0) return out;
    const std::size_t cells = std::size_t(s.frames) * s.bands;
    std::vector<float> alongTime(cells), acrossBands(cells);
    std::vector<float> window;
    window.reserve(64);

    // Along time, band by band.
    for (int b = 0; b < s.bands; ++b) {
        if (progress.cancelled()) return {};
        progress.at(0.5 * b / s.bands);
        for (int f = 0; f < s.frames; ++f) {
            window.clear();
            for (int g = std::max(0, f - HarmonicHalfFrames); g <= std::min(s.frames - 1, f + HarmonicHalfFrames); ++g)
                window.push_back(s.at(g, b));
            alongTime[std::size_t(f) * s.bands + b] = medianOf(window);
        }
    }
    // Across bands, frame by frame.
    for (int f = 0; f < s.frames; ++f) {
        if ((f & 1023) == 0) {
            if (progress.cancelled()) return {};
            progress.at(0.5 + 0.5 * f / s.frames);
        }
        for (int b = 0; b < s.bands; ++b) {
            window.clear();
            for (int c = std::max(0, b - PercussiveHalfBands); c <= std::min(s.bands - 1, b + PercussiveHalfBands); ++c)
                window.push_back(s.at(f, c));
            acrossBands[std::size_t(f) * s.bands + b] = medianOf(window);
        }
    }
    out.percussive.resize(cells);
    out.harmonic.resize(cells);
    for (std::size_t i = 0; i < cells; ++i) {
        const float h = alongTime[i] * alongTime[i], p = acrossBands[i] * acrossBands[i];
        const float mask = p / std::max(p + h, 1e-20f);
        out.percussive[i] = s.magnitude[i] * mask;
        out.harmonic[i] = s.magnitude[i] * (1.0f - mask);
    }
    progress.at(1.0);
    return out;
}

} // namespace analysis
