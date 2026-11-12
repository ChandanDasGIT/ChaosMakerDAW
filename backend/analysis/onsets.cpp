#include "analysis/onsets.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace analysis {

float sampleAt(const std::vector<float>& values, double frame) noexcept
{
    if (values.empty() || frame < 0.0 || frame > double(values.size() - 1)) return 0.0f;
    const auto i = std::size_t(frame);
    if (i + 1 >= values.size()) return values[i];
    const float t = float(frame - double(i));
    return values[i] * (1.0f - t) + values[i + 1] * t;
}

float peakNear(const std::vector<float>& values, double frame, int half) noexcept
{
    const long centre = std::lround(frame);
    float best = 0.0f;
    for (long f = centre - half; f <= centre + half; ++f)
        if (f >= 0 && f < long(values.size())) best = std::max(best, values[std::size_t(f)]);
    return best;
}

float percentile(std::vector<float> values, double fraction)
{
    if (values.empty()) return 0.0f;
    const auto at = std::size_t(std::clamp(fraction, 0.0, 1.0) * double(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(at), values.end());
    return values[at];
}

Envelopes envelopes(const BandSpectrogram& s, const std::vector<float>& percussive)
{
    Envelopes e;
    e.frameRate = s.frameRate();
    if (s.frames <= 0 || percussive.size() != s.magnitude.size()) return e;
    const auto frames = std::size_t(s.frames);
    for (auto* v : {&e.total, &e.low, &e.mid, &e.high, &e.highLevel, &e.lowPlain}) v->assign(frames, 0.0f);

    // Log scale relative to the loud end of this sound: a quiet recording reads like a loud one.
    std::vector<float> nonZero;
    nonZero.reserve(percussive.size() / 4);
    for (std::size_t i = 0; i < percussive.size(); i += 4)
        if (percussive[i] > 0.0f) nonZero.push_back(percussive[i]);
    const float reference = std::max(percentile(std::move(nonZero), 0.99), 1e-9f);
    const float gamma = 100.0f / reference;

    const int lowFrom = s.bandAt(LowFromHz), lowTo = s.bandAt(LowToHz);
    const int midFrom = s.bandAt(MidFromHz), midTo = s.bandAt(MidToHz);
    const int highFrom = s.bandAt(HighFromHz), highTo = s.bands;
    const auto count = [](int from, int to) { return float(std::max(1, to - from)); };

    std::vector<float> previous(std::size_t(s.bands), 0.0f), current(std::size_t(s.bands));
    float lowBefore = 0.0f;
    for (std::size_t f = 0; f < frames; ++f) {
        float total = 0.0f, low = 0.0f, mid = 0.0f, high = 0.0f, level = 0.0f, lowNow = 0.0f;
        for (int b = 0; b < s.bands; ++b) {
            current[b] = std::log1p(gamma * percussive[f * s.bands + b]);
            const float rise = f == 0 ? 0.0f : std::max(0.0f, current[b] - previous[b]);
            total += rise;
            if (b >= lowFrom && b < lowTo) { low += rise; lowNow += percussive[f * s.bands + b]; }
            if (b >= midFrom && b < midTo) mid += rise;
            if (b >= highFrom && b < highTo) {
                high += rise;
                level += s.at(int(f), b);
            }
        }
        e.total[f] = total / float(s.bands);
        e.low[f] = low / count(lowFrom, lowTo);
        e.mid[f] = mid / count(midFrom, midTo);
        e.high[f] = high / count(highFrom, highTo);
        e.highLevel[f] = level / count(highFrom, highTo);
        e.lowPlain[f] = f == 0 ? 0.0f : std::max(0.0f, lowNow - lowBefore);
        lowBefore = lowNow;
        std::swap(previous, current);
    }
    return e;
}

std::vector<float> harmonicChange(const BandSpectrogram& s, const std::vector<float>& harmonic)
{
    std::vector<float> change;
    if (s.frames <= 0 || harmonic.size() != s.magnitude.size()) return change;
    std::vector<float> sample;
    for (std::size_t i = 0; i < harmonic.size(); i += 4)
        if (harmonic[i] > 0.0f) sample.push_back(harmonic[i]);
    const float reference = std::max(percentile(std::move(sample), 0.99), 1e-9f);
    const float gamma = 100.0f / reference;
    constexpr int Lag = 8;
    change.assign(std::size_t(s.frames), 0.0f);
    for (int f = Lag; f < s.frames; ++f) {
        float sum = 0.0f;
        for (int b = 0; b < s.bands; ++b) {
            const float now = std::log1p(gamma * harmonic[std::size_t(f) * s.bands + b]);
            const float before = std::log1p(gamma * harmonic[std::size_t(f - Lag) * s.bands + b]);
            sum += std::max(0.0f, now - before);
        }
        change[std::size_t(f)] = sum / float(s.bands);
    }
    return change;
}

std::vector<Onset> pickOnsets(const Envelopes& e)
{
    std::vector<Onset> onsets;
    const auto n = int(e.total.size());
    if (n < 3) return onsets;
    const float delta = 0.1f * percentile(e.total, 0.95);
    constexpr int ThresholdHalf = 15;      // the median of +-150 ms
    constexpr int PeakHalf = 3;            // a peak is the largest within +-30 ms
    std::vector<float> window;
    window.reserve(2 * ThresholdHalf + 1);
    int last = -1000;
    for (int f = 1; f < n - 1; ++f) {
        const float v = e.total[std::size_t(f)];
        if (v <= delta) continue;
        bool isPeak = true;
        for (int g = std::max(0, f - PeakHalf); g <= std::min(n - 1, f + PeakHalf) && isPeak; ++g)
            if (e.total[std::size_t(g)] > v || (e.total[std::size_t(g)] == v && g < f)) isPeak = false;
        if (!isPeak) continue;
        window.clear();
        for (int g = std::max(0, f - ThresholdHalf); g <= std::min(n - 1, f + ThresholdHalf); ++g)
            window.push_back(e.total[std::size_t(g)]);
        const auto mid = window.begin() + std::ptrdiff_t(window.size() / 2);
        std::nth_element(window.begin(), mid, window.end());
        if (v < *mid + delta) continue;
        if (f - last < PeakHalf) continue;
        last = f;
        Onset o;
        o.frame = f;
        o.seconds = f / e.frameRate;
        o.total = v;
        o.low = peakNear(e.low, f, 1);
        o.mid = peakNear(e.mid, f, 1);
        o.high = peakNear(e.high, f, 1);
        onsets.push_back(o);
    }
    return onsets;
}

} // namespace analysis
