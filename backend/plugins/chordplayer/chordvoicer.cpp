#include "plugins/chordplayer/chordvoicer.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>

namespace chords {
namespace {

int nearestDistance(int note, std::span<const int> others) noexcept
{
    int best = INT_MAX;
    for (int o : others) best = std::min(best, std::abs(note - o));
    return best;
}

// How hard the hand works from `from` to `to`: with the same number of notes each finger
// goes to its counterpart (lowest to lowest ...); otherwise each note to its nearest. Squared,
// so one leap costs more than several small steps. Both are ascending.
double leadingCost(std::span<const int> from, std::span<const int> to) noexcept
{
    double cost = 0;
    if (from.size() == to.size()) {
        for (std::size_t i = 0; i < from.size(); ++i) cost += double(from[i] - to[i]) * double(from[i] - to[i]);
        return cost;
    }
    for (int n : to) { const int d = nearestDistance(n, from); cost += double(d) * d; }
    for (int n : from) { const int d = nearestDistance(n, to); cost += double(d) * d; }
    return cost;
}

} // namespace

std::vector<int> chordTones(const Chord& chord)
{
    std::vector<int> intervals = chord.intervals;
    if (intervals.size() > 4) {
        const auto fifth = std::find(intervals.begin(), intervals.end(), 7);
        if (fifth != intervals.end()) intervals.erase(fifth);
        intervals.resize(std::min<std::size_t>(intervals.size(), 4));
    }
    std::vector<int> out;
    for (int i : intervals) {
        const int pc = (chord.root + i) % 12;
        if (std::find(out.begin(), out.end(), pc) == out.end()) out.push_back(pc);
    }
    return out;
}

Voicing voiceTones(std::span<const int> pcs, const Voicing& previous, Range range) noexcept
{
    Voicing best;
    const std::size_t n = std::min<std::size_t>(pcs.size(), 4);
    if (n == 0 || range.high - range.low < 12) return best;
    const std::span<const int> before(previous.notes.data(), std::size_t(std::clamp(previous.count, 0, 4)));
    const double middle = (range.low + range.high) / 2.0;
    double bestScore = 1e18;
    // Every inversion, in close position, at every octave that fits the range.
    for (std::size_t inversion = 0; inversion < n; ++inversion) {
        std::array<int, 4> shape{};
        int last = -1;
        for (std::size_t i = 0; i < n; ++i) {
            int note = ((pcs[(inversion + i) % n] % 12) + 12) % 12;
            while (note <= last) note += 12;
            shape[i] = note;
            last = note;
        }
        for (int octave = -1; octave <= 10; ++octave) {
            Voicing candidate;
            candidate.count = int(n);
            double centre = 0;
            for (std::size_t i = 0; i < n; ++i) { candidate.notes[i] = shape[i] + 12 * octave; centre += candidate.notes[i]; }
            if (candidate.notes[0] < range.low || candidate.notes[n - 1] > range.high) continue;
            centre /= double(n);
            const std::span<const int> now(candidate.notes.data(), n);
            const double score = before.empty() ? std::abs(centre - middle)
                                                : leadingCost(before, now) + 0.01 * std::abs(centre - middle);
            if (score < bestScore) { bestScore = score; best = candidate; }
        }
    }
    return best;
}

std::vector<int> voice(const Chord& chord, const std::vector<int>& previous, Range range)
{
    const auto pcs = chordTones(chord);
    Voicing before;
    auto sorted = previous;
    std::sort(sorted.begin(), sorted.end());
    before.count = int(std::min<std::size_t>(sorted.size(), 4));
    for (int i = 0; i < before.count; ++i) before.notes[std::size_t(i)] = sorted[std::size_t(i)];
    const Voicing v = voiceTones(pcs, before, range);
    return std::vector<int>(v.notes.begin(), v.notes.begin() + v.count);
}

int bassNote(int pc, int previousBass, Range range) noexcept
{
    pc = ((pc % 12) + 12) % 12;
    const double target = previousBass >= 0 ? previousBass : (range.low + range.high) / 2.0;
    int best = -1;
    double bestDistance = 1e9;
    for (int note = range.low; note <= range.high; ++note) {
        if (note % 12 != pc) continue;
        const double d = std::abs(note - target);
        if (d < bestDistance) { bestDistance = d; best = note; }
    }
    return best;
}

int bassNote(const Chord& chord, int previousBass, Range range)
{
    return bassNote(chord.bassClass(), previousBass, range);
}

int movement(const std::vector<int>& from, const std::vector<int>& to)
{
    if (from.empty() || to.empty()) return 0;
    int total = 0;
    for (int n : to) total += nearestDistance(n, from);
    for (int n : from) total += nearestDistance(n, to);
    return total;
}

} // namespace chords
