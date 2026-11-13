#include "analysis/tonefeatures.h"

#include "analysis/chromagram.h"
#include "dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

namespace analysis {
namespace {

constexpr double QuietBelowDb = 45.0;      // under the loud parts: not counted

double dbOf(double power) { return 10.0 * std::log10(power + 1e-30); }

// The sound's level in 10 ms steps, in dB.
std::vector<double> levelSteps(const std::vector<float>& x, double rate)
{
    const auto step = std::size_t(std::max(1.0, rate / 100.0));
    std::vector<double> out;
    for (std::size_t at = 0; at + step <= x.size(); at += step) {
        double sum = 0.0;
        for (std::size_t i = at; i < at + step; ++i) sum += double(x[i]) * x[i];
        out.push_back(dbOf(sum / double(step)));
    }
    return out;
}

// Where the playing stops (from a loud part the level only falls for a second, by 20 dB or
// more, with no new note): the time to fall 30 dB from there. The shortest of the three
// longest stops' times; -1 without a stop.
double tailOf(const std::vector<double>& levels, double loudDb)
{
    std::vector<double> tails;
    const int n = int(levels.size());
    for (int i = 0; i + 100 < n; ++i) {
        const double from = levels[std::size_t(i)];
        if (from < loudDb - 12.0 || levels[std::size_t(i + 100)] > from - 20.0) continue;
        bool falls = true;
        for (int j = i + 5; j <= i + 100 && falls; ++j) falls = levels[std::size_t(j)] <= from - 2.0;
        if (!falls) continue;
        int j = i;
        while (j < n && levels[std::size_t(j)] > from - 30.0) ++j;
        if (j < n) tails.push_back((j - i) / 100.0);
        i += 100;
    }
    if (tails.empty()) return -1.0;
    std::sort(tails.begin(), tails.end(), std::greater<>());
    if (tails.size() > 3) tails.resize(3);
    return tails.back();
}

} // namespace

double ToneFeatures::bandHz(int band) noexcept { return 1000.0 * std::pow(2.0, (band - 11) / 3.0); }

ToneFeatures measureTone(const std::vector<float>& x, double rate, bool withNotes)
{
    ToneFeatures out;
    if (!(rate > 8000.0) || double(x.size()) < rate) return out;
    const auto levels = levelSteps(x, rate);
    std::vector<double> sorted = levels;
    std::sort(sorted.begin(), sorted.end());
    const double loudDb = sorted[std::size_t(0.95 * double(sorted.size() - 1))];
    if (loudDb < -90.0) return out;
    const double floorDb = loudDb - QuietBelowDb;

    // The balance: the mean spectrum of the frames with sound.
    int size = 2;
    while (size < int(0.17 * rate)) size *= 2;
    const Fft fft(size);
    std::vector<std::complex<double>> bins(static_cast<std::size_t>(size));
    std::vector<double> window(static_cast<std::size_t>(size)), power(static_cast<std::size_t>(size / 2 + 1), 0.0);
    for (int i = 0; i < size; ++i) window[std::size_t(i)] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / size);
    int frames = 0;
    for (std::size_t start = 0; start + std::size_t(size) <= x.size(); start += std::size_t(size / 2)) {
        double sum = 0.0;
        for (int i = 0; i < size; ++i) sum += double(x[start + std::size_t(i)]) * x[start + std::size_t(i)];
        if (dbOf(sum / size) < floorDb) continue;
        for (int i = 0; i < size; ++i) bins[std::size_t(i)] = {x[start + std::size_t(i)] * window[std::size_t(i)], 0.0};
        fft.forward(bins.data());
        for (std::size_t k = 0; k < power.size(); ++k) power[k] += std::norm(bins[k]);
        ++frames;
    }
    if (frames == 0) return out;
    const double binHz = rate / size;
    double mean = 0.0;
    for (int b = 0; b < ToneFeatures::Bands; ++b) {
        const double centre = ToneFeatures::bandHz(b);
        const double lo = centre / std::pow(2.0, 1.0 / 6.0), hi = centre * std::pow(2.0, 1.0 / 6.0);
        double sum = 0.0;
        for (std::size_t k = 1; k < power.size(); ++k)
            if (double(k) * binHz >= lo && double(k) * binHz < hi) sum += power[k];
        out.balanceDb[std::size_t(b)] = std::max(-120.0, dbOf(sum));
        mean += out.balanceDb[std::size_t(b)];
    }
    mean /= ToneFeatures::Bands;
    for (double& v : out.balanceDb) v -= mean;

    // The crest and the level, over 50 ms windows with sound.
    const auto win = std::size_t(rate / 20.0);
    std::vector<double> crests;
    double total = 0.0;
    std::size_t counted = 0;
    for (std::size_t start = 0; start + win <= x.size(); start += win) {
        double peak = 0.0, sum = 0.0;
        for (std::size_t i = start; i < start + win; ++i) { peak = std::max(peak, double(std::abs(x[i]))); sum += double(x[i]) * x[i]; }
        if (dbOf(sum / double(win)) < floorDb) continue;
        crests.push_back(20.0 * std::log10(peak / std::sqrt(sum / double(win)) + 1e-30));
        total += sum;
        counted += win;
    }
    if (crests.empty() || double(counted) < 0.5 * rate) return out;
    std::sort(crests.begin(), crests.end());
    out.crestDb = crests[crests.size() / 2];
    out.levelDb = dbOf(total / double(counted));
    out.soundSeconds = double(counted) / rate;
    out.valid = out.soundSeconds >= 1.0;
    if (!withNotes) return out;
    out.tailSeconds = tailOf(levels, loudDb);

    // Chords or single notes: how many note names stand out at once.
    const Chromagram chroma = chromagram(x, rate);
    int chordFrames = 0, soundFrames = 0;
    for (int f = 0; f < chroma.frames; ++f) {
        const auto at = std::size_t(std::min<double>(double(levels.size()) - 1.0, f / chroma.frameRate * 100.0));
        if (levels.empty() || levels[at] < floorDb + 15.0) continue;
        const auto& row = chroma.notes[std::size_t(f)];
        const float top = *std::max_element(row.begin(), row.end());
        if (!(top > 0.0f)) continue;
        int names = 0;
        for (float v : row) names += v >= 0.4f * top ? 1 : 0;
        ++soundFrames;
        chordFrames += names >= 3 ? 1 : 0;
    }
    out.chordShare = soundFrames > 0 ? double(chordFrames) / soundFrames : 0.0;
    return out;
}

double balanceDistance(const ToneFeatures& a, const ToneFeatures& b, double fromHz, double toHz)
{
    double meanA = 0.0, meanB = 0.0;
    int count = 0;
    for (int i = 0; i < ToneFeatures::Bands; ++i) {
        const double hz = ToneFeatures::bandHz(i);
        if (hz < fromHz * 0.99 || hz > toHz * 1.01) continue;
        meanA += a.balanceDb[std::size_t(i)];
        meanB += b.balanceDb[std::size_t(i)];
        ++count;
    }
    if (count == 0) return 0.0;
    meanA /= count;
    meanB /= count;
    double sum = 0.0;
    for (int i = 0; i < ToneFeatures::Bands; ++i) {
        const double hz = ToneFeatures::bandHz(i);
        if (hz < fromHz * 0.99 || hz > toHz * 1.01) continue;
        const double d = (a.balanceDb[std::size_t(i)] - meanA) - (b.balanceDb[std::size_t(i)] - meanB);
        sum += d * d;
    }
    return std::sqrt(sum / count);
}

} // namespace analysis
