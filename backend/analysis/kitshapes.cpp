#include "analysis/kitshapes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace analysis {
namespace {

using Shape = std::vector<float>;

double dot(const Shape& a, const Shape& b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) sum += double(a[i]) * b[i];
    return sum;
}

double cosine(const Shape& a, const Shape& b)
{
    const double d = std::sqrt(dot(a, a) * dot(b, b));
    return d > 0.0 ? dot(a, b) / d : 0.0;
}

double distance2(const Shape& a, const Shape& b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) sum += (double(a[i]) - b[i]) * (double(a[i]) - b[i]);
    return sum;
}

void normalise(Shape& v)
{
    double sum = 0.0;
    for (float x : v) sum += x;
    if (sum > 0.0)
        for (float& x : v) x = float(x / sum);
}

// The share of `shape` (sums to 1) between two frequencies.
double share(const Shape& shape, const BandSpectrogram& s, double fromHz, double toHz)
{
    double sum = 0.0;
    for (int b = 0; b < s.bands; ++b)
        if (s.centreHz[b] >= fromHz && s.centreHz[b] < toHz) sum += shape[std::size_t(b)];
    return sum;
}

using Group = KitGroup;

// x as a non-negative sum of the columns `basis`: the weights (multiplicative updates).
std::vector<double> fit(const Shape& x, const std::vector<const Shape*>& basis)
{
    std::vector<double> w(basis.size(), 1.0 / std::max<std::size_t>(1, basis.size()));
    std::vector<double> ax(basis.size());
    for (std::size_t j = 0; j < basis.size(); ++j) ax[j] = dot(*basis[j], x);
    for (int it = 0; it < 200; ++it)
        for (std::size_t j = 0; j < basis.size(); ++j) {
            double aaw = 0.0;
            for (std::size_t k = 0; k < basis.size(); ++k) aaw += dot(*basis[j], *basis[k]) * w[k];
            w[j] *= aaw > 1e-20 ? ax[j] / aaw : 0.0;
        }
    return w;
}

} // namespace

KitShapes learnKit(const BandSpectrogram& s, const std::vector<float>& cells)
{
    KitShapes kit;
    const int F = s.frames, B = s.bands;
    if (F < 8 || B <= 0 || cells.size() != std::size_t(F) * B) return kit;

    // 1. The hits.
    std::vector<float> rise(std::size_t(F), 0.0f);
    for (int f = 1; f < F; ++f) {
        float sum = 0.0f;
        for (int b = 0; b < B; ++b) sum += std::max(0.0f, cells[std::size_t(f) * B + b] - cells[std::size_t(f - 1) * B + b]);
        rise[std::size_t(f)] = sum;
    }
    std::vector<float> sorted = rise;
    std::sort(sorted.begin(), sorted.end());
    const float strong = sorted[std::size_t(0.98 * (F - 1))];
    if (!(strong > 0.0f)) return kit;
    std::vector<int> hits;
    for (int f = 3; f < F - 3; ++f) {
        const float v = rise[std::size_t(f)];
        if (v < KitHitAt * strong) continue;
        bool peak = true;
        for (int g = f - 3; g <= f + 3 && peak; ++g)
            if (rise[std::size_t(g)] > v || (rise[std::size_t(g)] == v && g < f)) peak = false;
        if (peak) hits.push_back(f);
    }
    const int N = int(hits.size());
    kit.hits = N;
    if (N < KitMinHits) return kit;

    // 2. Each hit's rising spectrum: `spectra` to tell the sounds apart (levels raised by
    // KitContrast, so the shapes stand out), `plain` as the factorisation will see them.
    std::vector<Shape> spectra, plain;
    spectra.assign(std::size_t(N), Shape(std::size_t(B), 0.0f));
    plain = spectra;
    std::vector<double> levels;
    levels.assign(std::size_t(N), 0.0);
    for (int i = 0; i < N; ++i) {
        const int f = hits[std::size_t(i)];
        for (int b = 0; b < B; ++b) {
            float top = 0.0f;
            for (int g = f; g <= f + 2; ++g) top = std::max(top, cells[std::size_t(g) * B + b]);
            const float before = cells[std::size_t(f - 2) * B + b];
            plain[std::size_t(i)][std::size_t(b)] = std::max(0.0f, top - before);
            spectra[std::size_t(i)][std::size_t(b)] = std::max(0.0f, std::pow(top, KitContrast) - std::pow(before, KitContrast));
            levels[std::size_t(i)] += plain[std::size_t(i)][std::size_t(b)];
        }
        normalise(plain[std::size_t(i)]);
        normalise(spectra[std::size_t(i)]);
    }
    // The groups: all hits as one, then the most spread-out group split in two, again and again.
    std::vector<int> of(std::size_t(N), 0);
    std::vector<Shape> centres(1, Shape(std::size_t(B), 0.0f));
    const auto centreOf = [&](int k) {
        Shape sum(std::size_t(B), 0.0f);
        for (int i = 0; i < N; ++i)
            if (of[std::size_t(i)] == k)
                for (int b = 0; b < B; ++b) sum[std::size_t(b)] += spectra[std::size_t(i)][std::size_t(b)];
        normalise(sum);
        return sum;
    };
    centres[0] = centreOf(0);
    const int K = std::clamp(N / 4, 3, KitGroups);
    while (int(centres.size()) < K) {
        int widest = -1;
        double most = 0.0;
        for (int k = 0; k < int(centres.size()); ++k) {
            double spread = 0.0;
            int count = 0;
            for (int i = 0; i < N; ++i)
                if (of[std::size_t(i)] == k) { spread += distance2(spectra[std::size_t(i)], centres[std::size_t(k)]); ++count; }
            if (count >= 4 && spread > most) { most = spread; widest = k; }
        }
        if (widest < 0) break;
        // Two starts: the hit farthest from the centre, and the hit farthest from that one.
        int a = -1, c = -1;
        double far = -1.0;
        for (int i = 0; i < N; ++i)
            if (of[std::size_t(i)] == widest) {
                const double d = distance2(spectra[std::size_t(i)], centres[std::size_t(widest)]);
                if (d > far) { far = d; a = i; }
            }
        far = -1.0;
        for (int i = 0; i < N; ++i)
            if (of[std::size_t(i)] == widest) {
                const double d = distance2(spectra[std::size_t(i)], spectra[std::size_t(a)]);
                if (d > far) { far = d; c = i; }
            }
        const int fresh = int(centres.size());
        Shape ca = spectra[std::size_t(a)], cc = spectra[std::size_t(c)];
        std::vector<int> members;
        for (int i = 0; i < N; ++i)
            if (of[std::size_t(i)] == widest) members.push_back(i);
        centres.push_back(cc);
        for (int it = 0; it < 12; ++it) {
            for (int i : members)
                of[std::size_t(i)] = distance2(spectra[std::size_t(i)], ca) <= distance2(spectra[std::size_t(i)], cc) ? widest : fresh;
            ca = centreOf(widest);
            cc = centreOf(fresh);
        }
        centres[std::size_t(widest)] = ca;
        centres[std::size_t(fresh)] = cc;
    }
    // All groups settle together.
    for (int it = 0; it < 10; ++it) {
        for (int i = 0; i < N; ++i) {
            double best = 1e30;
            for (int k = 0; k < int(centres.size()); ++k) {
                const double d = distance2(spectra[std::size_t(i)], centres[std::size_t(k)]);
                if (d < best) { best = d; of[std::size_t(i)] = k; }
            }
        }
        for (int k = 0; k < int(centres.size()); ++k) centres[std::size_t(k)] = centreOf(k);
    }
    std::vector<Group> groups;
    for (int k = 0; k < int(centres.size()); ++k) {
        Group g;
        g.centre = centres[std::size_t(k)];
        g.plain.assign(std::size_t(B), 0.0f);
        for (int i = 0; i < N; ++i)
            if (of[std::size_t(i)] == k) {
                ++g.count;
                g.level += levels[std::size_t(i)];
                for (int b = 0; b < B; ++b) g.plain[std::size_t(b)] += plain[std::size_t(i)][std::size_t(b)];
            }
        if (g.count < 2) continue;                          // a one-off noise
        g.level /= g.count;
        normalise(g.plain);
        groups.push_back(std::move(g));
    }
    // Groups that sound alike are one sound (a drum hit softly and hard).
    for (std::size_t i = 0; i < groups.size(); ++i)
        for (std::size_t j = i + 1; j < groups.size();) {
            if (cosine(groups[i].centre, groups[j].centre) < KitSame) { ++j; continue; }
            const int all = groups[i].count + groups[j].count;
            for (int b = 0; b < B; ++b)
                groups[i].centre[std::size_t(b)] = (groups[i].centre[std::size_t(b)] * groups[i].count + groups[j].centre[std::size_t(b)] * groups[j].count) / all;
            for (int b = 0; b < B; ++b)
                groups[i].plain[std::size_t(b)] = (groups[i].plain[std::size_t(b)] * groups[i].count + groups[j].plain[std::size_t(b)] * groups[j].count) / all;
            groups[i].level = (groups[i].level * groups[i].count + groups[j].level * groups[j].count) / all;
            groups[i].count = all;
            groups.erase(groups.begin() + std::ptrdiff_t(j));
        }

    // 3. Two sounds together: well made of the other groups, with at least two of them in it.
    const int minPart = std::max(3, int(std::lround(KitPartHits * N)));
    for (std::size_t i = 0; i < groups.size(); ++i) {
        std::vector<const Shape*> basis;
        for (std::size_t j = 0; j < groups.size(); ++j)
            if (j != i && groups[j].count >= minPart) basis.push_back(&groups[j].centre);   // a part is heard alone often enough
        if (basis.size() < 2) continue;
        const auto w = fit(groups[i].centre, basis);
        Shape model(std::size_t(B), 0.0f);
        int parts = 0;
        for (std::size_t j = 0; j < basis.size(); ++j) {
            if (w[j] >= KitPart) ++parts;
            for (int b = 0; b < B; ++b) model[std::size_t(b)] += float(w[j] * (*basis[j])[std::size_t(b)]);
        }
        const double error = std::sqrt(distance2(groups[i].centre, model) / std::max(1e-20, dot(groups[i].centre, groups[i].centre)));
        groups[i].mix = parts >= 2 && error < KitMixError;
    }

    // 4. Which is which, of the sounds heard alone.
    const auto pick = [&](auto&& score) {
        Group* best = nullptr;
        double most = 0.0;
        for (auto& g : groups) {
            if (g.mix || g.role >= 0) continue;
            const double v = score(g);
            if (v > most) { most = v; best = &g; }
        }
        return best;
    };
    // The kick: lows, and the most played and loudest of those.
    if (Group* kick = pick([&](const Group& g) {
            const double lows = share(g.centre, s, 0.0, 150.0);
            return lows > KitKickLows ? lows * g.count * g.level : 0.0;
        }))
        kick->role = 0;
    // The hi-hat: the brightest with hardly any lows; failing that, the brightest that is
    // brighter than it is low.
    Group* hat = pick([&](const Group& g) {
        const double highs = share(g.centre, s, 4000.0, 1e9);
        return highs > KitHatHighs && share(g.centre, s, 0.0, 200.0) < KitHatLows ? highs : 0.0;
    });
    if (!hat)
        hat = pick([&](const Group& g) {
            const double highs = share(g.centre, s, 4000.0, 1e9);
            return highs > KitHatHighs && highs > share(g.centre, s, 0.0, 200.0) ? highs : 0.0;
        });
    if (hat) hat->role = 2;
    // The snare: the most played and loudest of the rest that has a body.
    if (Group* snare = pick([&](const Group& g) {
            return share(g.centre, s, 150.0, 1500.0) >= KitSnareBody ? g.count * g.level : 0.0;
        }))
        snare->role = 1;
    for (auto& g : groups)
        if (!g.mix && g.role < 0) g.role = 3;
    kit.groups = std::move(groups);
    return kit;
}

} // namespace analysis
