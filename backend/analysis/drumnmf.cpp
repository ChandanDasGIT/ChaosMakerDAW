#include "analysis/drumnmf.h"

#include "analysis/kitshapes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace analysis {
namespace {

double bump(double hz, double centre, double widthOctaves)
{
    const double x = std::log2(hz / centre) / widthOctaves;
    return std::exp(-0.5 * x * x);
}

void normalise(std::vector<float>& v)
{
    double sum = 0.0;
    for (float x : v) sum += x;
    if (sum > 0.0)
        for (float& x : v) x = float(x / sum);
}

} // namespace

std::vector<float> drumTemplate(Drum drum, const BandSpectrogram& s, bool drumsOnly)
{
    std::vector<float> t(static_cast<std::size_t>(s.bands));
    for (int b = 0; b < s.bands; ++b) {
        const double f = s.centreHz[b];
        double v = 0.0;
        switch (drum) {
        case KickDrum:                     // a low thump, a little beater click
            v = bump(f, 65.0, 0.6) + 0.15 * bump(f, 180.0, 0.7) + 0.04 * bump(f, 3000.0, 1.0);
            break;
        case SnareDrum:
            if (drumsOnly)                 // as measured on real kits: mostly the body, the noise falling off upwards
                v = bump(f, 280.0, 0.8) + 0.25 * bump(f, 1200.0, 1.2) + 0.08 / (1.0 + std::exp(-4.0 * std::log2(f / 3000.0)));
            else                           // in a song: a little body, mostly the crack and the wires' noise, up high too
                v = 0.4 * bump(f, 200.0, 0.4) + 0.6 * bump(f, 1500.0, 0.9) + 0.6 * bump(f, 4500.0, 0.8)
                  + 0.3 / (1.0 + std::exp(-4.0 * std::log2(f / 8000.0)));
            break;
        case HatDrum:
            if (drumsOnly)                 // noise from the low mids up, brightest on top
                v = 0.3 / (1.0 + std::exp(-4.0 * std::log2(f / 400.0))) + 0.7 / (1.0 + std::exp(-4.0 * std::log2(f / 5000.0)));
            else                           // in a song: bright noise only
                v = 1.0 / (1.0 + std::exp(-4.0 * std::log2(f / 6500.0))) * (f < 400.0 ? 0.0 : 1.0);
            break;
        default:
            break;
        }
        t[std::size_t(b)] = float(v + 1e-4);
    }
    normalise(t);
    return t;
}

namespace {

// One spectral shape of the factorisation.
struct Part {
    std::vector<float> start;              // sums to 1
    int drum = -1;                         // a Drum, or -1: something else (a tom, what is left)
    float keepFrom = 0.0f, keepTo = 0.0f;  // how firmly it is pulled back to `start`: first to last iteration
};

// The level of each part per frame (parts x frames); empty when cancelled.
std::vector<float> factorise(const BandSpectrogram& s, const std::vector<float>& cells, const std::vector<Part>& parts,
                             const Progress& progress)
{
    const int F = s.frames, B = s.bands, K = int(parts.size());
    // W: bands x parts (columns sum to 1); H: parts x frames.
    std::vector<float> W(std::size_t(B) * K), H(std::size_t(K) * F);
    for (int k = 0; k < K; ++k)
        for (int b = 0; b < B; ++b) W[std::size_t(b) * K + k] = parts[std::size_t(k)].start[std::size_t(b)];
    for (int f = 0; f < F; ++f) {
        double total = 0.0;
        for (int b = 0; b < B; ++b) total += cells[std::size_t(f) * B + b];
        for (int k = 0; k < K; ++k) H[std::size_t(k) * F + f] = float(total / K + 1e-9);
    }

    std::vector<float> ratio(std::size_t(F) * B);           // V / (W H), frame-major
    const auto computeRatio = [&] {
        for (int f = 0; f < F; ++f)
            for (int b = 0; b < B; ++b) {
                double model = 0.0;
                for (int k = 0; k < K; ++k) model += double(W[std::size_t(b) * K + k]) * H[std::size_t(k) * F + f];
                const float v = cells[std::size_t(f) * B + b];
                ratio[std::size_t(f) * B + b] = model > 1e-12 ? float(v / model) : 0.0f;
            }
    };
    for (int it = 0; it < NmfIterations; ++it) {
        if (progress.cancelled()) return {};
        progress.at(double(it) / NmfIterations);
        // H <- H * (W^T ratio) / (W^T 1); the columns of W sum to 1.
        computeRatio();
        for (int k = 0; k < K; ++k)
            for (int f = 0; f < F; ++f) {
                double num = 0.0;
                for (int b = 0; b < B; ++b) num += double(W[std::size_t(b) * K + k]) * ratio[std::size_t(f) * B + b];
                H[std::size_t(k) * F + f] *= float(num);
            }
        // W <- W * (ratio H^T) / (1 H^T), then each shape is pulled back towards its start and
        // every column is scaled to sum 1.
        computeRatio();
        for (int k = 0; k < K; ++k) {
            const Part& part = parts[std::size_t(k)];
            double hSum = 0.0;
            for (int f = 0; f < F; ++f) hSum += H[std::size_t(k) * F + f];
            if (hSum <= 1e-12) continue;
            std::vector<float> col(static_cast<std::size_t>(B));
            for (int b = 0; b < B; ++b) {
                double num = 0.0;
                for (int f = 0; f < F; ++f) num += double(ratio[std::size_t(f) * B + b]) * H[std::size_t(k) * F + f];
                col[b] = float(W[std::size_t(b) * K + k] * num / hSum);
            }
            normalise(col);
            const float keep = part.keepFrom + (part.keepTo - part.keepFrom) * float(it) / NmfIterations;
            if (keep > 0.0f) {
                for (int b = 0; b < B; ++b) col[b] = keep * part.start[std::size_t(b)] + (1.0f - keep) * col[b];
                normalise(col);
            }
            for (int b = 0; b < B; ++b) W[std::size_t(b) * K + k] = col[b];
        }
    }
    return H;
}

} // namespace

DrumActivations drumActivations(const BandSpectrogram& s, const std::vector<float>& linear, bool drumsOnly,
                                 const Progress& progress)
{
    DrumActivations out;
    out.frameRate = s.frameRate();
    if (s.frames <= 0 || linear.size() != std::size_t(s.frames) * s.bands) return out;
    const int F = s.frames, B = s.bands;
    // Drums only: compressed levels (a hi-hat is far quieter than a kick, and must still
    // count). A song: plain levels, as its other instruments would otherwise count more too.
    const float power = drumsOnly ? Compression : 1.0f;
    std::vector<float> cells(linear);
    if (drumsOnly)
        for (float& v : cells) v = std::pow(v, power);

    // The parts: this recording's own sounds (kitshapes), held firmly; a generic shape that
    // adapts a little for a drum that was not found; and free shapes for what is left.
    const KitShapes kit = drumsOnly ? learnKit(s, cells) : KitShapes{};
    std::vector<Part> parts;
    for (const auto& g : kit.groups)
        if (g.role >= 0 && g.role < DrumCount) parts.push_back({g.plain, g.role, OwnKeep, OwnKeep});
    int others = 0;                                         // the kit's other sounds start the free shapes
    for (const auto& g : kit.groups)
        if (g.role >= DrumCount && others < FreeShapes) { parts.push_back({g.plain, -1, 0.0f, 0.0f}); ++others; }
    for (int d = 0; d < DrumCount; ++d) {
        if (kit.has(d)) continue;
        Part part{drumTemplate(Drum(d), s, drumsOnly), d, 0.85f, 0.4f};
        for (float& v : part.start) v = std::pow(v, power);
        normalise(part.start);
        parts.push_back(std::move(part));
    }
    std::uint32_t seed = 0x2545F491u;
    for (int k = others; k < FreeShapes; ++k) {
        Part part;
        part.start.resize(std::size_t(B));
        for (int b = 0; b < B; ++b) {
            seed = seed * 1664525u + 1013904223u;
            // Free shapes stay below FreeShapesBelowHz: what is bright is the drums'.
            part.start[std::size_t(b)] = s.centreHz[b] < FreeShapesBelowHz ? 0.5f + float(seed >> 8) / float(1u << 24) : 0.0f;
        }
        normalise(part.start);
        parts.push_back(std::move(part));
    }
    const std::vector<float> H = factorise(s, cells, parts, progress);
    if (H.empty()) return {};

    for (int d = 0; d < DrumCount; ++d) {
        auto& level = out.level[d];
        auto& rise = out.rise[d];
        level.assign(std::size_t(F), 0.0f);
        for (std::size_t k = 0; k < parts.size(); ++k)
            if (parts[k].drum == d)
                for (int f = 0; f < F; ++f) level[std::size_t(f)] += H[k * F + f];
        rise.assign(std::size_t(F), 0.0f);
        for (int f = 1; f < F; ++f) rise[f] = std::max(0.0f, level[f] - level[f - 1]);
    }
    progress.at(1.0);
    return out;
}

} // namespace analysis
