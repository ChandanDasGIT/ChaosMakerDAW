#include "analysis/beattracker.h"

#include "analysis/onsets.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace analysis {
namespace {

double combScore(const std::vector<float>& e, double period, double phase, double limit)
{
    double sum = 0.0;
    int count = 0;
    const double end = std::min(double(e.size()) - 1.0, limit);
    for (double t = phase; t < end; t += period) {
        sum += sampleAt(e, t);
        ++count;
    }
    return count > 0 ? sum / count : 0.0;
}

struct Fit { double period, phase, score; };

Fit bestPhase(const std::vector<float>& e, double period, double from, double to, double step, double limit)
{
    Fit best{period, 0.0, -1.0};
    for (double phase = from; phase < to; phase += step) {
        const double p = std::fmod(std::fmod(phase, period) + period, period);
        const double score = combScore(e, period, p, limit);
        if (score > best.score) best = {period, p, score};
    }
    return best;
}

} // namespace

std::vector<float> beatEnvelope(const std::vector<float>& envelope, double frameRate)
{
    const auto n = envelope.size();
    std::vector<float> e(n, 0.0f);
    if (n == 0) return e;
    // Minus the moving mean over +-0.25 s, rectified, then a 3-frame smoothing so a beat a
    // fraction of a frame off still lines up with its neighbours.
    const int half = std::max(1, int(std::lround(0.25 * frameRate)));
    std::vector<double> prefix(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + envelope[i];
    std::vector<float> rectified(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto lo = std::size_t(std::max<long>(0, long(i) - half));
        const auto hi = std::min(n, i + std::size_t(half) + 1);
        const double mean = (prefix[hi] - prefix[lo]) / double(hi - lo);
        rectified[i] = float(std::max(0.0, envelope[i] - mean));
    }
    for (std::size_t i = 0; i < n; ++i) {
        const float a = i > 0 ? rectified[i - 1] : 0.0f, c = i + 1 < n ? rectified[i + 1] : 0.0f;
        e[i] = 0.25f * a + 0.5f * rectified[i] + 0.25f * c;
    }
    double sumSq = 0.0;
    for (float v : e) sumSq += double(v) * v;
    const double rms = std::sqrt(sumSq / double(n));
    if (rms > 0.0)
        for (float& v : e) v = float(v / rms);
    return e;
}

Beat trackBeats(const std::vector<float>& envelope, double frameRate, double nearBpm)
{
    Beat beat;
    beat.frameRate = frameRate;
    const auto e = beatEnvelope(envelope, frameRate);
    const auto n = long(e.size());
    const double minBpm = nearBpm > 0.0 ? nearBpm / 1.05 : MinBpm;
    const double maxBpm = nearBpm > 0.0 ? nearBpm * 1.05 : MaxBpm;
    const double lagMin = 60.0 * frameRate / maxBpm, lagMax = 60.0 * frameRate / minBpm;
    if (n < long(4 * lagMax) || frameRate <= 0.0) return beat;

    // Autocorrelation up to four times the longest period.
    const long maxLag = std::min(n - 1, long(std::ceil(4.0 * lagMax)) + 2);
    std::vector<double> ac(std::size_t(maxLag + 1), 0.0);
    for (long lag = 1; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (long t = 0; t + lag < n; ++t) sum += double(e[std::size_t(t)]) * e[std::size_t(t + lag)];
        ac[std::size_t(lag)] = sum / double(n - lag);
    }
    const auto acAt = [&](double lag) {
        if (lag < 1.0 || lag >= double(maxLag)) return 0.0;
        const auto i = std::size_t(lag);
        const double t = lag - double(i);
        return ac[i] * (1.0 - t) + ac[i + 1] * t;
    };

    double bestLag = 0.0, bestScore = -1e300;
    for (double lag = lagMin; lag <= lagMax; lag += 0.25) {
        // Its multiples count only as far as the period itself repeats: a pulse half-way
        // between the beats must not win on the beats' own repeats.
        const double own = acAt(lag);
        double score = own;
        for (int k = 2; k <= 4; ++k) score += std::min(own, acAt(k * lag)) / k;
        if (nearBpm <= 0.0) {
            const double octaves = std::log2(60.0 * frameRate / lag / PreferredBpm);
            score *= std::exp(-0.5 * octaves * octaves / (0.9 * 0.9));
        }
        if (score > bestScore) { bestScore = score; bestLag = lag; }
    }
    if (bestLag <= 0.0) return beat;

    // The comb, first over the opening 30 s: coarse period +-2 % with every phase, then fine.
    // A long song needs the period far more exactly (a 0.1 % error drifts by beats over five
    // minutes), so the window then doubles until it holds the whole sound, each time searching
    // the period within what the shorter window left open.
    double limit = std::min(double(n), 30.0 * frameRate);
    Fit best{bestLag, 0.0, -1.0};
    for (double d = -0.02; d <= 0.02 + 1e-9; d += 0.002) {
        const double period = bestLag * (1.0 + d);
        if (period < lagMin * 0.98 || period > lagMax * 1.02) continue;
        const auto fit = bestPhase(e, period, 0.0, period, 0.5, limit);
        if (fit.score > best.score) best = fit;
    }
    {
        const Fit coarse = best;
        for (double d = -0.0025; d <= 0.0025 + 1e-9; d += 0.00025) {
            const auto fit = bestPhase(e, coarse.period * (1.0 + d), coarse.phase - 1.0, coarse.phase + 1.0, 0.1, limit);
            if (fit.score > best.score) best = fit;
        }
    }
    while (limit < double(n)) {
        const double beats = limit / best.period;                  // in the window just fitted
        limit = std::min(double(n), 2.0 * limit);
        const Fit around = best;
        const double span = 2.0 / beats;                           // frames of period left open
        best.score = -1.0;
        for (int i = -20; i <= 20; ++i) {
            const auto fit = bestPhase(e, around.period + span * i / 20.0, around.phase - 1.0, around.phase + 1.0, 0.1, limit);
            if (fit.score > best.score) best = fit;
        }
    }
    // A whole-number tempo when it is that close.
    const double bpm = 60.0 * frameRate / best.period;
    if (std::abs(bpm - std::round(bpm)) < 0.12) {
        const double period = 60.0 * frameRate / std::round(bpm);
        const Fit snapped = bestPhase(e, period, best.phase - 1.5, best.phase + 1.5, 0.05, double(n));
        // Kept only when it fits about as well (a song a little off the whole number stays).
        if (snapped.score >= 0.9 * best.score) best = snapped;
    }
    beat.period = best.period;
    beat.phase = best.phase;
    beat.strength = best.score;
    return beat;
}

} // namespace analysis
