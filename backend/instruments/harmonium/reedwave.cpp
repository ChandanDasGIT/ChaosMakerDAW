#include "instruments/harmonium/reedwave.h"
#include "dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace reedwave {

namespace {

constexpr int LevelCount = int(HarmonicCounts.size());
constexpr std::size_t Stride = TableSize + 1;

std::size_t offset(Timbre timbre, bool full, int level)
{
    return ((std::size_t(timbre) * 2 + (full ? 1 : 0)) * LevelCount + std::size_t(level)) * Stride;
}

// The Indian reed is modelled on how a free reed sounds: each swing lets one puff of air
// through the slot. The puff is a smooth, lopsided pulse (it opens in 30 % of its width and
// closes in the rest), and what we hear is how fast that air flow changes (the pulse's
// derivative). More air makes the puff shorter, so the tone gets brighter; low air gives a
// wide, round puff. Widths fitted to single notes of the references in
// local-reference/harmonium (overtones rise to about the 3rd, then fall away steadily).
constexpr double FullPuffWidth = 0.30;                       // fraction of the period
constexpr double SoftPuffWidth = 0.55;
constexpr double PuffRise = 0.30;                            // of the puff's width

// Complex harmonic h (1-based) of the reed, unscaled: the phases keep the puff's shape.
const std::vector<std::complex<double>>& reedHarmonics(bool full)
{
    static const auto build = [](double width) {
        const Fft fft(TableSize);
        std::vector<std::complex<double>> x(TableSize);
        const double rise = width * PuffRise, fall = width - rise;
        for (int i = 0; i < TableSize; ++i) {
            const double t = double(i) / TableSize;
            double v = 0.0;
            if (t < rise) v = 0.5 - 0.5 * std::cos(std::numbers::pi * t / rise);
            else if (t < width) v = 0.5 + 0.5 * std::cos(std::numbers::pi * (t - rise) / fall);
            x[std::size_t(i)] = v;
        }
        fft.forward(x.data());
        std::vector<std::complex<double>> h(HarmonicCounts.back() + 1);
        double peak = 0.0;
        for (std::size_t k = 1; k < h.size(); ++k) {
            h[k] = std::complex<double>(0.0, double(k)) * x[k];   // the derivative: i k X[k]
            peak = std::max(peak, std::abs(h[k]));
        }
        for (auto& v : h) v /= peak;
        return h;
    };
    static const auto fullHarmonics = build(FullPuffWidth);
    static const auto softHarmonics = build(SoftPuffWidth);
    return full ? fullHarmonics : softHarmonics;
}

} // namespace

double harmonicAmplitude(Timbre timbre, bool full, int h) noexcept
{
    if (h < 1) return 0.0;
    const double n = double(h);
    double a = 0.0, soften = 0.0;                            // soften: overtone fall at low air
    switch (timbre) {
    case Timbre::Reed:
        return h <= HarmonicCounts.back() ? std::abs(reedHarmonics(full)[std::size_t(h)]) : 0.0;
    case Timbre::Diapason:
        a = std::pow(n, -0.9);
        soften = 4.0;
        break;
    case Timbre::Dulciana:
        a = std::pow(n, -1.4);
        soften = 3.0;
        break;
    case Timbre::Bourdon:
        a = std::pow(n, -1.2) * (h % 2 == 0 ? 0.4 : 1.0);
        soften = 3.0;
        break;
    }
    if (!full) a *= std::exp(-(n - 1.0) / soften);
    return a;
}

Tables::Tables()
    : m_data(std::size_t(TimbreCount) * 2 * LevelCount * Stride, 0.0f)
{
    const Fft fft(TableSize);
    std::vector<std::complex<double>> bins(TableSize);
    for (int t = 0; t < TimbreCount; ++t) {
        const auto timbre = Timbre(t);
        for (int f = 0; f < 2; ++f) {
            const bool full = f == 1;
            // One scale for all levels of a timbre, from the richest table: a high note then
            // only loses the overtones it cannot hold, not loudness.
            double scale = 0.0;
            for (int level = LevelCount - 1; level >= 0; --level) {
                // a sin(h x + p) is bin h = -i e^(ip) a N / 2 and its mirror; inverse() divides
                // by N. Schroeder phases p keep the organ stops' peaks low for their spectrum.
                std::fill(bins.begin(), bins.end(), std::complex<double>{});
                for (int h = 1; h <= HarmonicCounts[std::size_t(level)]; ++h) {
                    const double a = harmonicAmplitude(timbre, full, h) * TableSize / 2.0;
                    const double p = std::numbers::pi * double(h) * double(h - 1) / 256.0;
                    // The reed keeps its puff's own phases; the organ stops use Schroeder phases.
                    const auto bin = timbre == Timbre::Reed
                        ? reedHarmonics(full)[std::size_t(h)] * (TableSize / 2.0)
                        : std::complex<double>(0.0, -a) * std::polar(1.0, p);
                    bins[std::size_t(h)] = bin;
                    bins[std::size_t(TableSize - h)] = std::conj(bin);
                }
                fft.inverse(bins.data());
                if (level == LevelCount - 1) {
                    double sum = 0.0;
                    for (const auto& v : bins) sum += v.real() * v.real();
                    scale = 0.25 / std::sqrt(sum / TableSize);   // full table at -12 dB RMS
                }
                float* out = m_data.data() + offset(timbre, full, level);
                for (int i = 0; i < TableSize; ++i) out[i] = float(bins[std::size_t(i)].real() * scale);
                out[TableSize] = out[0];
            }
        }
    }
}

const Tables& Tables::instance()
{
    static const Tables tables;                              // thread-safe, built once
    return tables;
}

const float* Tables::table(Timbre timbre, bool full, int level) const noexcept
{
    level = std::clamp(level, 0, LevelCount - 1);
    return m_data.data() + offset(timbre, full, level);
}

int Tables::levelFor(double frequencyHz, double sampleRate) noexcept
{
    if (!(frequencyHz > 0.0) || !(sampleRate > 0.0)) return 0;
    const double allowed = TopHarmonicLimit * sampleRate / frequencyHz;
    int level = 0;
    for (int l = 0; l < LevelCount; ++l)
        if (double(HarmonicCounts[std::size_t(l)]) <= allowed) level = l;
    return level;
}

void Oscillator::start(Timbre timbre, double frequencyHz, double sampleRate, double phase) noexcept
{
    const auto& tables = Tables::instance();
    m_level = Tables::levelFor(frequencyHz, sampleRate);
    m_soft = tables.table(timbre, false, m_level);
    m_full = tables.table(timbre, true, m_level);
    m_phase = phase - std::floor(phase);
    setFrequency(frequencyHz, sampleRate);
}

void Oscillator::setFrequency(double frequencyHz, double sampleRate) noexcept
{
    m_increment = sampleRate > 0.0 ? std::clamp(frequencyHz / sampleRate, 0.0, 0.5) : 0.0;
}

} // namespace reedwave
