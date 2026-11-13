#include "analysis/chromagram.h"

#include "dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace analysis {
namespace {

struct Peak {
    double hz;
    float level;
};

// The peaks of one frame's spectrum between two bins: local maxima above a part of the
// frame's largest, their frequency from a parabola through the peak and its neighbours.
void findPeaks(const float* row, int bins, double binHz, int firstBin, std::vector<Peak>& peaks)
{
    peaks.clear();
    float top = 0.0f;
    for (int k = 0; k < bins; ++k) top = std::max(top, row[k]);
    if (!(top > 0.0f)) return;
    const float floor = 0.02f * top;
    for (int k = 1; k + 1 < bins; ++k) {
        const float a = row[k - 1], b = row[k], c = row[k + 1];
        if (b < floor || b <= a || b < c) continue;
        const double curve = double(a) - 2.0 * b + c;
        const double shift = curve < 0.0 ? std::clamp(0.5 * (double(a) - c) / curve, -0.5, 0.5) : 0.0;
        peaks.push_back({(firstBin + k + shift) * binHz, float(b - 0.25 * (double(a) - c) * shift)});
    }
}

// A frequency as a note number (69 = the A of 440 Hz), in semitones.
double noteOf(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

} // namespace

const char* noteName(int pitchClass) noexcept
{
    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[((pitchClass % 12) + 12) % 12];
}

Chroma chromaBetween(const std::vector<Chroma>& rows, double frameRate, double fromSeconds, double toSeconds)
{
    Chroma out{};
    if (rows.empty() || !(frameRate > 0.0)) return out;
    const long last = long(rows.size()) - 1;
    long from = long(std::ceil(fromSeconds * frameRate - 1e-9)), to = long(std::ceil(toSeconds * frameRate - 1e-9)) - 1;
    if (to < from) from = to = std::lround(0.5 * (fromSeconds + toSeconds) * frameRate);
    from = std::clamp(from, 0L, last);
    to = std::clamp(to, from, last);
    for (long f = from; f <= to; ++f)
        for (int c = 0; c < 12; ++c) out[std::size_t(c)] += rows[std::size_t(f)][std::size_t(c)];
    const float top = *std::max_element(out.begin(), out.end());
    if (top > 0.0f)
        for (float& v : out) v /= top;
    return out;
}

Chromagram chromagram(const std::vector<float>& mono, double sampleRate, const Progress& progress)
{
    Chromagram out;
    if (!(sampleRate > 8000.0) || double(mono.size()) < sampleRate) return out;
    const int hop = std::max(1, int(std::lround(sampleRate / 20.0)));
    int size = 2;
    while (size < int(0.17 * sampleRate)) size *= 2;       // 8192 at 44.1 / 48 kHz
    const double binHz = sampleRate / size;
    const int firstBin = std::max(1, int(std::floor(ChromaLowestHz / binHz)) - 1);
    const int lastBin = std::min(size / 2 - 1, int(std::ceil(ChromaHighestHz / binHz)) + 1);
    const int bins = lastBin - firstBin + 1;
    if (bins < 8) return out;
    const int frames = int(mono.size() / std::size_t(hop)) + 1;

    // 1. The spectrum, the bins in range only.
    std::vector<float> window(static_cast<std::size_t>(size));
    for (int i = 0; i < size; ++i) window[std::size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / size));
    const Fft fft(size);
    std::vector<std::complex<double>> buffer(static_cast<std::size_t>(size));
    std::vector<float> spectrum(std::size_t(frames) * bins);
    const auto n = std::int64_t(mono.size());
    for (int f = 0; f < frames; ++f) {
        if ((f & 63) == 0) {
            if (progress.cancelled()) return {};
            progress.at(0.8 * f / frames);
        }
        const std::int64_t start = std::int64_t(f) * hop - size / 2;
        for (int i = 0; i < size; ++i) {
            const auto at = start + i;
            buffer[std::size_t(i)] = (at >= 0 && at < n) ? double(mono[std::size_t(at)] * window[std::size_t(i)]) : 0.0;
        }
        fft.forward(buffer.data());
        float* row = &spectrum[std::size_t(f) * bins];
        for (int k = 0; k < bins; ++k) row[k] = float(std::abs(buffer[std::size_t(firstBin + k)]) * (2.0 / size));
    }

    // 2. What stays: the median along time, bin by bin.
    std::vector<float> held(spectrum.size());
    {
        std::vector<float> span;
        span.reserve(2 * ChromaMedianHalfFrames + 1);
        for (int k = 0; k < bins; ++k) {
            if ((k & 63) == 0 && progress.cancelled()) return {};
            for (int f = 0; f < frames; ++f) {
                span.clear();
                for (int g = std::max(0, f - ChromaMedianHalfFrames); g <= std::min(frames - 1, f + ChromaMedianHalfFrames); ++g)
                    span.push_back(spectrum[std::size_t(g) * bins + k]);
                const auto mid = span.begin() + std::ptrdiff_t(span.size() / 2);
                std::nth_element(span.begin(), mid, span.end());
                held[std::size_t(f) * bins + k] = *mid;
            }
        }
    }
    progress.at(0.9);

    // 3. The peaks of every frame, and the loud end of the sound (for the log level).
    std::vector<std::vector<Peak>> peaks(static_cast<std::size_t>(frames));
    std::vector<float> levels;
    for (int f = 0; f < frames; ++f) {
        findPeaks(&held[std::size_t(f) * bins], bins, binHz, firstBin, peaks[std::size_t(f)]);
        for (const auto& p : peaks[std::size_t(f)]) levels.push_back(p.level);
    }
    out.frameRate = sampleRate / hop;
    out.frames = frames;
    out.notes.assign(std::size_t(frames), Chroma{});
    out.bass.assign(std::size_t(frames), Chroma{});
    if (levels.empty()) return out;                         // silence
    const auto loudAt = levels.begin() + std::ptrdiff_t(0.95 * double(levels.size() - 1));
    std::nth_element(levels.begin(), loudAt, levels.end());
    const float loud = std::max(*loudAt, 1e-9f);

    // 4. The tuning: each peak's distance from the nearest note of A = 440, as an angle; the
    // mean angle is the song's offset. Peaks from 110 Hz up (below, a bin is too wide to tell).
    {
        double re = 0.0, im = 0.0;
        for (const auto& frame : peaks)
            for (const auto& p : frame) {
                if (p.hz < 110.0 || p.hz > ChromaHighestHz) continue;
                const double note = noteOf(p.hz);
                const double off = note - std::round(note);
                const double weight = std::log1p(100.0 * p.level / loud);
                re += weight * std::cos(2.0 * std::numbers::pi * off);
                im += weight * std::sin(2.0 * std::numbers::pi * off);
            }
        if (re != 0.0 || im != 0.0) out.tuningCents = 100.0 * std::atan2(im, re) / (2.0 * std::numbers::pi);
    }
    const double tuning = out.tuningCents / 100.0;

    for (int f = 0; f < frames; ++f)
        for (const auto& p : peaks[std::size_t(f)]) {
            if (p.hz < ChromaLowestHz || p.hz > ChromaHighestHz) continue;
            // An overtone of a louder note below (its 3rd or 5th: a fifth or a major third
            // above, octaves up) is that note's colour, not a note of its own: it counts less.
            float own = 1.0f;
            for (const auto& q : peaks[std::size_t(f)]) {
                if (q.hz >= p.hz || q.level < p.level) continue;
                for (double times : {3.0, 5.0})
                    if (std::abs(12.0 * std::log2(p.hz / (times * q.hz))) < ChromaOvertoneWithin) own = ChromaOvertoneCounts;
            }
            const double note = noteOf(p.hz) - tuning;
            const double nearest = std::round(note);
            const double off = note - nearest;                       // -0.5 .. 0.5 semitones
            const double near = std::cos(std::numbers::pi * off);    // 1 on the note, 0 half-way to the next
            const float weight = own * float(near * near * std::log1p(100.0 * p.level / loud));
            const auto name = std::size_t(((long(nearest) % 12) + 12) % 12);
            if (p.hz >= ChromaNotesFromHz) out.notes[std::size_t(f)][name] += weight;
            if (p.hz < ChromaBassToHz) out.bass[std::size_t(f)][name] += weight;
        }
    progress.at(1.0);
    return out;
}

} // namespace analysis
