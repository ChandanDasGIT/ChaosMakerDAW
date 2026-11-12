#include "analysis/meterdetect.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace analysis {
namespace {

struct BeatFeatures {
    std::vector<double> low, mid;
};

// The kick and snare rise on each beat, each scaled to its mean so both count alike.
BeatFeatures beatFeatures(const Envelopes& e, const Beat& beat)
{
    BeatFeatures f;
    const double frames = double(e.total.size());
    const auto& low = e.kick.empty() ? e.low : e.kick;
    const auto& mid = e.snare.empty() ? e.mid : e.snare;
    for (int k = 0; beat.frameOf(k) < frames - 1.0; ++k) {
        f.low.push_back(peakNear(low, beat.frameOf(k), 2));
        f.mid.push_back(peakNear(mid, beat.frameOf(k), 2));
    }
    for (auto* v : {&f.low, &f.mid}) {
        double mean = 0.0;
        for (double x : *v) mean += x;
        mean = v->empty() ? 0.0 : mean / double(v->size());
        if (mean > 0.0)
            for (double& x : *v) x /= mean;
    }
    return f;
}

// How much the beats differ from those `lag` beats later: small when the pattern repeats.
double difference(const BeatFeatures& f, int lag)
{
    const auto n = int(f.low.size());
    if (n <= lag) return 1e9;
    double sum = 0.0;
    for (int k = 0; k + lag < n; ++k)
        sum += std::abs(f.low[k] - f.low[k + lag]) + std::abs(f.mid[k] - f.mid[k + lag]);
    return sum / double(n - lag);
}

double meanBetween(const std::vector<float>& e, const Beat& beat, double fraction)
{
    double sum = 0.0;
    int count = 0;
    for (int k = 0; beat.frameOf(k) + fraction * beat.period < double(e.size()) - 1.0; ++k) {
        sum += peakNear(e, beat.frameOf(k) + fraction * beat.period, 1);
        ++count;
    }
    return count > 0 ? sum / count : 0.0;
}

} // namespace

Meter findMeter(const Envelopes& e, const Beat& beat)
{
    Meter m;
    if (beat.strength <= 0.0 || e.total.empty()) return m;
    const auto f = beatFeatures(e, beat);

    // Duple or triple: what sits between the beats. Read on the drums' own rises, each scaled to
    // its typical hit, so quiet hi-hats count as much as the kick (the total rise otherwise).
    std::vector<float> all = e.total;
    if (!e.kick.empty() && e.kick.size() == e.total.size()) {
        std::fill(all.begin(), all.end(), 0.0f);
        for (const auto* drum : {&e.kick, &e.snare, &e.hat}) {
            const float typical = percentile(*drum, 0.99);
            if (!(typical > 0.0f)) continue;
            for (std::size_t i = 0; i < all.size() && i < drum->size(); ++i) all[i] += std::min(1.5f, (*drum)[i] / typical);
        }
    }
    const double onBeat = meanBetween(all, beat, 0.0);
    const double halves = meanBetween(all, beat, 0.5);
    const double thirds = 0.5 * (meanBetween(all, beat, 1.0 / 3.0) + meanBetween(all, beat, 2.0 / 3.0));
    const double quarters = 0.5 * (meanBetween(all, beat, 0.25) + meanBetween(all, beat, 0.75));
    m.triple = thirds > 1.3 * halves && thirds > 1.3 * quarters && thirds > 0.15 * onBeat;

    const double d2 = difference(f, 2), d3 = difference(f, 3), d4 = difference(f, 4);
    const double d6 = difference(f, 6), d7 = difference(f, 7), d8 = difference(f, 8);
    const bool fast = beat.bpm() >= 150.0;
    constexpr double Clearly = 0.85;
    if (m.triple) {
        m.beatQuarters = 1.5;
        m.denominator = 8;
        if (d3 < Clearly * std::min(d2, d4)) { m.beatsPerBar = 3; m.numerator = 9; }
        else if (d2 <= 1.1 * d4) { m.beatsPerBar = 2; m.numerator = 6; }
        else { m.beatsPerBar = 4; m.numerator = 12; }
    } else if (fast && d7 < Clearly * std::min({d4, d6, d8})) {
        m.beatQuarters = 0.5; m.beatsPerBar = 7; m.numerator = 7; m.denominator = 8;
    } else if (fast && d6 < Clearly * std::min(d4, d8) && d3 < d4) {
        m.beatQuarters = 0.5; m.beatsPerBar = 6; m.numerator = 6; m.denominator = 8;
    } else if (d3 < Clearly * std::min(d2, d4)) {
        m.beatsPerBar = 3; m.numerator = 3;
    }

    // The bar starts where the kick is strongest and the snare weakest, and (in a song) where the
    // chords change: each beat's change against the mean change.
    std::vector<double> change;
    if (!e.change.empty()) {
        double mean = 0.0;
        for (int k = 0; beat.frameOf(k) < double(e.change.size()) - 1.0; ++k) {
            change.push_back(peakNear(e.change, beat.frameOf(k), 4));
            mean += change.back();
        }
        mean = change.empty() ? 0.0 : mean / double(change.size());
        // Only as far as the sound has chords to speak of: none for a drum loop (whose drums'
        // ringing tails make up to about 40 % of it), fully from about half.
        const double weight = 0.7 * std::clamp((e.harmonicShare - 0.42) / 0.1, 0.0, 1.0);
        if (mean > 0.0 && weight > 0.0)
            for (double& c : change) c *= weight / mean;
        else change.clear();
    }
    double bestScore = -1e9;
    for (int p = 0; p < m.beatsPerBar; ++p) {
        double low = 0.0, mid = 0.0, chord = 0.0;
        int count = 0;
        for (std::size_t k = std::size_t(p); k < f.low.size(); k += std::size_t(m.beatsPerBar)) {
            low += f.low[k];
            mid += f.mid[k];
            if (k < change.size()) chord += change[k];
            ++count;
        }
        if (count == 0) continue;
        const double score = (low - 0.5 * mid + chord) / count;
        if (score > bestScore + 1e-9) { bestScore = score; m.downbeat = p; }
    }
    return m;
}

} // namespace analysis
