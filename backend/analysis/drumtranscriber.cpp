#include "analysis/drumtranscriber.h"

#include "analysis/hpss.h"
#include "analysis/spectrogram.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace analysis {
namespace {

// How much earlier than the hit its rise shows: measured 0 to 8 ms (the kits differ), well
// inside a grid cell, so none is added back.
constexpr double OnsetLeadSeconds = 0.0;

// A hit counts when it reaches this part of its drum's typical hit (and stands out locally).
// Drums only: the rises come from compressed levels, so the parts are larger.
constexpr float KickAt = 0.45f, SnareAt = 0.4f, HatAt = 0.035f;
constexpr float DrumsOnlyKickAt = 0.5f, DrumsOnlySnareAt = 0.5f, DrumsOnlyHatAt = 0.25f;

// The backbeat (see backbeatFactor): from snare hits this strong, this many at least; "often"
// is this share of the snare hits, "never" less than BackbeatHardly of that.
constexpr float BackbeatSnareAt = 0.5f;
constexpr int BackbeatMinSnares = 6;
constexpr double BackbeatClear = 0.5, BackbeatHardly = 0.15;
constexpr double BackbeatMinBpm = 50.0;
// Drums only when the harmonic part (chords, bass) is less than this share of the sound: a
// drum loop or stem (its ringing tails make up to about 40 %), not a song.
constexpr float DrumsOnlyBelow = 0.45f;
// The beats move onto the kick and snare when those weigh this much more between the beats.
constexpr double OffBeatClearly = 1.5;

int velocityOf(float strength)
{
    const double v = 0.3 + 0.7 * std::min(1.5, double(strength)) / 1.5;
    return std::clamp(int(std::lround(127.0 * v)), 1, 127);
}

// How often two snare hits lie `seconds` apart (within 35 ms), by their strength, per hit.
double snareRepeat(const std::vector<DrumHit>& snares, double seconds)
{
    double sum = 0.0, all = 0.0;
    for (std::size_t i = 0; i < snares.size(); ++i) {
        const double a = std::min(1.5, double(snares[i].strength));
        all += a;
        for (std::size_t j = i + 1; j < snares.size() && snares[j].seconds - snares[i].seconds < seconds + 0.035; ++j)
            if (std::abs(snares[j].seconds - snares[i].seconds - seconds) < 0.035) sum += std::min(a, std::min(1.5, double(snares[j].strength)));
    }
    return all > 0.0 ? sum / all : 0.0;
}

// The tracker's tempo may be the song's doubled. The snare tells: in most music it falls on
// every second beat (the backbeat). When the snare hits never lie one or two beats apart but
// often four, the beats are eighth notes: 0.5 (the tempo halves). Else 1. (The other way,
// a snare on every beat, is also just a snare on every beat: left alone.)
double backbeatFactor(const std::vector<DrumHit>& hits, const Beat& beat)
{
    std::vector<DrumHit> snares;
    for (const auto& h : hits)
        if (h.key == SnareKey && h.strength >= BackbeatSnareAt) snares.push_back(h);
    if (int(snares.size()) < BackbeatMinSnares || beat.strength <= 0.0) return 1.0;
    const double beatSeconds = beat.period / beat.frameRate;
    const double one = snareRepeat(snares, beatSeconds), two = snareRepeat(snares, 2.0 * beatSeconds);
    const double four = snareRepeat(snares, 4.0 * beatSeconds);
    // Halved only when it is plain: the snare never one or two beats apart, and often four.
    if (four < BackbeatClear || one > BackbeatHardly * four || two > BackbeatHardly * four) return 1.0;
    const double factor = 0.5;
    const double bpm = beat.bpm() * factor;
    return bpm >= BackbeatMinBpm && bpm <= MaxBpm ? factor : 1.0;
}

// The beats moved onto the kick and snare when those mostly fall between them (half-way, or on
// a third in a compound meter): a busy off-beat can pull the comb there.
bool moveOntoDrums(const std::vector<DrumHit>& hits, Beat& beat, bool triple)
{
    const int parts = triple ? 3 : 2;
    double weight[3] = {};
    for (const auto& h : hits) {
        if (h.key != KickKey && h.key != SnareKey) continue;
        const double x = (h.seconds * beat.frameRate - beat.phase) / beat.period * parts;
        const double nearest = std::round(x);
        if (std::abs(x - nearest) > 0.25) continue;         // between the candidates
        const int part = int(((long(nearest) % parts) + parts) % parts);
        weight[part] += std::min(1.5, double(h.strength));
    }
    int best = 0;
    for (int p = 1; p < parts; ++p)
        if (weight[p] > weight[best]) best = p;
    if (best == 0 || weight[best] < OffBeatClearly * weight[0]) return false;
    beat.phase = std::fmod(beat.phase + best * beat.period / parts, beat.period);
    return true;
}

// The meter of `a`'s beat, the beats moved onto the drums where needed, and the pattern.
void settle(DrumAnalysis& a)
{
    // The meter reads accents (which beat is the heavy one). Drums only: the drums' rises come
    // from compressed levels, which even those out, so it gets them squared, and the kick as
    // the lows' plain rise over 3 frames (how hard it was hit).
    Envelopes accents = a.envelopes;
    if (accents.drumsOnly) {
        for (auto* rise : {&accents.snare, &accents.hat})
            for (float& v : *rise) v *= v;
        if (accents.lowPlain.size() == accents.kick.size())
            for (std::size_t f = 0; f < accents.kick.size(); ++f)
                accents.kick[f] = accents.lowPlain[f] + (f > 0 ? accents.lowPlain[f - 1] : 0.0f) + (f + 1 < accents.kick.size() ? accents.lowPlain[f + 1] : 0.0f);
        else
            for (float& v : accents.kick) v *= v;
    }
    a.meter = findMeter(accents, a.beat);
    if (accents.drumsOnly && moveOntoDrums(a.hits, a.beat, a.meter.triple)) a.meter = findMeter(accents, a.beat);
    a.pattern = transcribe(a.hits, a.beat, a.meter);
}

} // namespace

std::vector<DrumHit> pickHits(const Envelopes& e)
{
    std::vector<DrumHit> hits;
    const std::vector<float>* rises[3] = {&e.kick, &e.snare, &e.hat};
    const int keys[3] = {KickKey, SnareKey, ClosedHatKey};
    const float at[3] = {e.drumsOnly ? DrumsOnlyKickAt : KickAt, e.drumsOnly ? DrumsOnlySnareAt : SnareAt,
                         e.drumsOnly ? DrumsOnlyHatAt : HatAt};
    float typical[3];
    for (int d = 0; d < 3; ++d) typical[d] = percentile(*rises[d], 0.99);
    // A drum the recording hardly has must not turn its noise into hits.
    const float loudest = std::max({typical[0], typical[1], typical[2]});
    for (int d = 0; d < 3; ++d) typical[d] = std::max(typical[d], 0.0005f * loudest);
    if (!(loudest > 0.0f)) return hits;

    constexpr int PeakHalf = 3, ThresholdHalf = 15;
    const int ring = int(std::lround(0.15 * e.frameRate));
    std::vector<float> window;
    for (int d = 0; d < 3; ++d) {
        const auto& r = *rises[d];
        const int n = int(r.size());
        for (int f = 1; f < n - 1; ++f) {
            const float v = r[std::size_t(f)];
            if (v < at[d] * typical[d]) continue;
            bool isPeak = true;
            for (int g = std::max(0, f - PeakHalf); g <= std::min(n - 1, f + PeakHalf) && isPeak; ++g)
                if (r[std::size_t(g)] > v || (r[std::size_t(g)] == v && g < f)) isPeak = false;
            if (!isPeak) continue;
            window.clear();
            for (int g = std::max(0, f - ThresholdHalf); g <= std::min(n - 1, f + ThresholdHalf); ++g) window.push_back(r[std::size_t(g)]);
            const auto mid = window.begin() + std::ptrdiff_t(window.size() / 2);
            std::nth_element(window.begin(), mid, window.end());
            if (v < *mid + 0.5f * at[d] * typical[d]) continue;
            int key = keys[d];
            if (d == 2) {                                   // open when the highs still ring 150 ms on
                const float start = peakNear(e.highLevel, f + 1, 1);
                const float later = sampleAt(e.highLevel, f + ring);
                if (start > 0.0f && later > 0.45f * start) key = OpenHatKey;
            }
            hits.push_back({f / e.frameRate + OnsetLeadSeconds, key, v / typical[d]});
        }
    }
    std::sort(hits.begin(), hits.end(), [](const DrumHit& a, const DrumHit& b) { return a.seconds < b.seconds; });
    // A kick and a snare together where one is weak and the other much stronger: the weak one is
    // the strong one's spill (a snare's body on the kick, a kick's click on the snare).
    std::vector<bool> spill(hits.size(), false);
    for (std::size_t i = 0; i < hits.size(); ++i)
        for (std::size_t j = 0; j < hits.size(); ++j) {
            if (i == j || std::abs(hits[i].seconds - hits[j].seconds) > 0.035) continue;
            const bool pair = (hits[i].key == KickKey && hits[j].key == SnareKey) || (hits[i].key == SnareKey && hits[j].key == KickKey);
            if (pair && hits[i].strength < 0.7f && hits[j].strength > 1.5f * hits[i].strength) spill[i] = true;
        }
    std::vector<DrumHit> kept;
    for (std::size_t i = 0; i < hits.size(); ++i)
        if (!spill[i]) kept.push_back(hits[i]);
    return kept;
}

DrumPattern transcribe(const std::vector<DrumHit>& hits, const Beat& beat, const Meter& meter)
{
    DrumPattern p;
    p.numerator = meter.numerator;
    p.denominator = meter.denominator;
    p.bpm = beat.bpm() * meter.beatQuarters;
    p.secondsPerQuarter = 60.0 / p.bpm;
    if (hits.empty() || beat.strength <= 0.0) return p;
    const double beatSeconds = beat.period / beat.frameRate;

    // The grid: the coarser of two unless many hits fall between its lines.
    const int coarse = meter.triple ? 3 : meter.beatQuarters < 1.0 ? 1 : 2;
    int divisions = coarse;
    {
        // Weighed by strength: faint leftovers of other instruments do not make the grid finer.
        const double cell = beatSeconds / coarse;
        double off = 0.0, all = 0.0;
        for (const auto& h : hits) {
            const double x = (h.seconds - beat.secondsOf(0)) / cell;
            const double weight = std::min(1.5, double(h.strength));
            all += weight;
            if (std::abs(x - std::round(x)) > 0.25) off += weight;
        }
        if (off > 0.12 * all) divisions = 2 * coarse;
    }
    const double cellSeconds = beatSeconds / divisions;
    p.gridQuarters = meter.beatQuarters / divisions;

    // Bar 1: the last bar line at or before the first hit (half a cell early still counts).
    const double barSeconds = beatSeconds * meter.beatsPerBar;
    const double someBar = beat.secondsOf(meter.downbeat);
    const double first = hits.front().seconds;
    const double bars = std::floor((first + 0.5 * cellSeconds - someBar) / barSeconds);
    p.firstBarSeconds = someBar + bars * barSeconds;

    std::map<std::pair<long, int>, float> cells;          // (cell, key) -> strongest hit
    for (const auto& h : hits) {
        const long cell = std::lround((h.seconds - p.firstBarSeconds) / cellSeconds);
        if (cell < 0) continue;
        auto& s = cells[{cell, h.key}];
        s = std::max(s, h.strength);
    }
    // A steady hi-hat line goes on under the kick and snare: the line's usual step is the most
    // common gap between hats; a kick or snare with no hat, but with hats one step before and one
    // step after it, gets the hat that it hid. Nothing is added on an empty spot.
    {
        std::vector<long> hats;
        for (const auto& [where, strength] : cells)
            if (where.second == ClosedHatKey || where.second == OpenHatKey) hats.push_back(where.first);
        std::sort(hats.begin(), hats.end());
        hats.erase(std::unique(hats.begin(), hats.end()), hats.end());
        std::map<long, int> gaps;
        for (std::size_t i = 1; i < hats.size(); ++i) ++gaps[hats[i] - hats[i - 1]];
        long step = 0;
        int most = 0;
        for (const auto& [gap, count] : gaps)
            if (count > most) { most = count; step = gap; }
        const auto hatAt = [&](long cell) -> float {
            for (int key : {ClosedHatKey, OpenHatKey})
                if (auto it = cells.find({cell, key}); it != cells.end()) return it->second;
            return -1.0f;
        };
        const auto drumAt = [&](long cell) { return cells.count({cell, KickKey}) > 0 || cells.count({cell, SnareKey}) > 0; };
        if (step > 0 && most >= 4) {
            // The line may run twice as fast, every other hat hidden under a kick or snare (their
            // own sound in this recording then has the hat in it): when the spots half-way
            // between the hats mostly hold a kick or snare and are seldom empty.
            if (step % 2 == 0) {
                int hidden = 0, open = 0;
                for (long hat : hats) {
                    if (hatAt(hat + step) < 0.0f || hatAt(hat + step / 2) >= 0.0f) continue;
                    if (drumAt(hat + step / 2)) ++hidden;
                    else ++open;
                }
                if (hidden >= 3 && hidden >= 2 * open) step /= 2;
            }
            // Twice: two hidden hats in a row are found one after the other.
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<std::pair<long, float>> add;
                for (const auto& [where, strength] : cells) {
                    if (where.second != KickKey && where.second != SnareKey) continue;
                    const long cell = where.first;
                    if (hatAt(cell) >= 0.0f) continue;
                    const float before = hatAt(cell - step);
                    float after = hatAt(cell + step);
                    if (after < 0.0f && pass == 0 && drumAt(cell + step)) after = hatAt(cell + 2 * step);
                    if (before >= 0.0f && after >= 0.0f) add.push_back({cell, 0.5f * (before + after)});
                }
                for (const auto& [cell, strength] : add) cells[{cell, ClosedHatKey}] = strength;
            }
        }
    }
    const double length = std::min(0.25, p.gridQuarters);
    long lastCell = 0;
    for (const auto& [where, strength] : cells) {
        MidiNote n;
        n.id = int(p.notes.size()) + 1;
        n.note = where.second;
        n.startBeat = double(where.first) * p.gridQuarters;
        n.lengthBeats = length;
        n.velocity = velocityOf(strength);
        p.notes.push_back(n);
        lastCell = std::max(lastCell, where.first);
        if (n.note == KickKey) ++p.kicks;
        else if (n.note == SnareKey) ++p.snares;
        else ++p.hats;
    }
    p.bars = int(std::floor(double(lastCell) * p.gridQuarters / p.barQuarters() + 1e-9)) + 1;
    return p;
}

std::vector<MidiNote> typicalBars(const DrumPattern& pattern, int count, int* startBar)
{
    if (startBar) *startBar = 0;
    if (count <= 0 || pattern.notes.empty()) return {};
    if (pattern.bars <= count) return pattern.notes;
    const double bar = pattern.barQuarters();
    const auto cellOf = [&](double beat) { return long(std::lround(beat / pattern.gridQuarters)); };
    const long cellsPerWindow = std::lround(count * bar / pattern.gridQuarters);
    std::vector<std::set<std::pair<long, int>>> windows;
    for (int s = 0; s + count <= pattern.bars; ++s) {
        std::set<std::pair<long, int>> w;
        const long from = std::lround(s * bar / pattern.gridQuarters);
        for (const auto& n : pattern.notes) {
            const long c = cellOf(n.startBeat) - from;
            if (c >= 0 && c < cellsPerWindow) w.insert({c, n.note});
        }
        windows.push_back(std::move(w));
    }
    int best = 0;
    double bestScore = -1.0;
    for (std::size_t s = 0; s < windows.size(); ++s) {
        if (windows[s].empty()) continue;
        double score = 0.0;
        for (std::size_t t = 0; t < windows.size(); ++t) {
            if (t == s || windows[t].empty()) continue;
            std::size_t common = 0;
            for (const auto& c : windows[s]) common += windows[t].count(c);
            score += 2.0 * double(common) / double(windows[s].size() + windows[t].size());
        }
        if (score > bestScore + 1e-9) { bestScore = score; best = int(s); }
    }
    if (startBar) *startBar = best;
    std::vector<MidiNote> notes;
    const double from = best * bar, to = (best + count) * bar;
    for (const auto& n : pattern.notes)
        if (n.startBeat >= from - 1e-9 && n.startBeat < to - 1e-9) {
            MidiNote m = n;
            m.startBeat -= from;
            m.id = int(notes.size()) + 1;
            notes.push_back(m);
        }
    return notes;
}

std::optional<DrumAnalysis> analyseDrums(const std::vector<float>& mono, double sampleRate, const Progress& progress)
{
    if (!(sampleRate > 1000.0) || double(mono.size()) < MinSeconds * sampleRate) return std::nullopt;
    DrumAnalysis a;
    a.seconds = double(mono.size()) / sampleRate;
    const auto spectrogram = bandSpectrogram(mono, sampleRate, progress.sub(0.0, 0.55));
    if (progress.cancelled() || spectrogram.frames == 0) return std::nullopt;
    const auto parts = separate(spectrogram, progress.sub(0.55, 0.8));
    if (progress.cancelled() || parts.percussive.empty()) return std::nullopt;
    a.envelopes = envelopes(spectrogram, parts.percussive);
    a.envelopes.change = harmonicChange(spectrogram, parts.harmonic);
    {
        double harmonic = 0.0, whole = 0.0;
        for (std::size_t i = 0; i < parts.harmonic.size(); ++i) { harmonic += parts.harmonic[i]; whole += spectrogram.magnitude[i]; }
        a.envelopes.harmonicShare = whole > 0.0 ? float(harmonic / whole) : 0.0f;
    }
    a.envelopes.drumsOnly = a.envelopes.harmonicShare < DrumsOnlyBelow;
    const auto drums = drumActivations(spectrogram, parts.percussive, a.envelopes.drumsOnly, progress.sub(0.8, 0.97));
    if (progress.cancelled() || drums.rise[0].empty()) return std::nullopt;
    for (int d = 0; d < DrumCount; ++d) {
        auto& out = d == KickDrum ? a.envelopes.kick : d == SnareDrum ? a.envelopes.snare : a.envelopes.hat;
        const auto& r = drums.rise[std::size_t(d)];
        out.assign(r.size(), 0.0f);
        for (std::size_t f = 0; f < r.size(); ++f)
            out[f] = r[f] + (f > 0 ? r[f - 1] : 0.0f) + (f + 1 < r.size() ? r[f + 1] : 0.0f);
    }
    a.hits = pickHits(a.envelopes);
    progress.at(0.98);
    if (progress.cancelled()) return std::nullopt;
    a.beat = trackBeats(a.envelopes.total, a.envelopes.frameRate);
    if (const double factor = a.envelopes.drumsOnly ? backbeatFactor(a.hits, a.beat) : 1.0; factor != 1.0) {
        const Beat other = trackBeats(a.envelopes.total, a.envelopes.frameRate, a.beat.bpm() * factor);
        if (other.strength > 0.0) a.beat = other;
    }
    settle(a);
    progress.at(1.0);
    return a;
}

std::optional<DrumAnalysis> retempo(const DrumAnalysis& analysis, double factor)
{
    const double target = analysis.beat.bpm() * factor;
    if (!(factor > 0.0) || target < 30.0 || target > 320.0 || analysis.envelopes.total.empty()) return std::nullopt;
    DrumAnalysis a = analysis;
    a.beat = trackBeats(a.envelopes.total, a.envelopes.frameRate, target);
    if (a.beat.strength <= 0.0) return std::nullopt;
    settle(a);
    return a;
}

} // namespace analysis
