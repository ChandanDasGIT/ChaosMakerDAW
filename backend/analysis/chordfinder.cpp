#include "analysis/chordfinder.h"

#include "analysis/hpss.h"
#include "analysis/onsets.h"
#include "analysis/spectrogram.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>

namespace analysis {
namespace {

constexpr int Qualities = 5;               // Major .. Minor7
constexpr int States = 1 + 12 * Qualities; // 0: no chord

// What a change of chord costs, in the units of a beat's likeness to a chord (0..1); less at
// the start of a bar, where most changes fall.
// What a change of chord costs, in the units of a beat's likeness to a chord (0..1); less at
// the start of a bar, where most changes fall.
constexpr double ChangeCost = 0.4, ChangeCostOnBar = 0.3;
// A seventh must beat its plain chord by this to be named.
constexpr double SeventhCost = 0.08;
// The chord's root in the bass counts this much.
constexpr double BassWeight = 0.12;
// The key's own chords, second time round.
constexpr double InKeyBonus = 0.03;
// A beat this quiet against the song's typical beat is "no chord".
constexpr double SilentBelow = 0.08;
// A drone: the share of a note's level that is there in the song's quietest moments of it.
constexpr double DroneQuantile = 0.2, DroneTaken = 0.8;
// Each overtone is this much quieter than the one before.
constexpr double OvertoneFall = 0.5;
// Each beat is read with this many beats either side of it: one beat alone is too little to go by.
constexpr int Smooth = 1;

struct Shape {
    std::array<int, 4> tones;
    int count;
};
constexpr Shape shapes[Qualities] = {
    {{0, 4, 7, 0}, 3}, {{0, 3, 7, 0}, 3}, {{0, 4, 7, 10}, 4}, {{0, 4, 7, 11}, 4}, {{0, 3, 7, 10}, 4},
};

int rootOf(int state) { return (state - 1) / Qualities; }
int qualityOf(int state) { return (state - 1) % Qualities; }       // into shapes
bool isSeventh(int state) { return state > 0 && qualityOf(state) >= 2; }

// A chord's notes with their overtones (the 3rd lands a fifth up, the 5th a major third up),
// scaled to length 1. A seventh's top note counts a little less than the triad's.
Chroma templateOf(int state)
{
    Chroma t{};
    const Shape& shape = shapes[qualityOf(state)];
    constexpr int overtoneClass[6] = {0, 0, 7, 0, 4, 7};
    for (int i = 0; i < shape.count; ++i) {
        const double tone = i == 3 ? 0.7 : 1.0;
        double level = 1.0;
        for (int h = 0; h < 6; ++h) {
            t[std::size_t((rootOf(state) + shape.tones[std::size_t(i)] + overtoneClass[h]) % 12)] += float(tone * level);
            level *= OvertoneFall;
        }
    }
    double sum = 0.0;
    for (float v : t) sum += double(v) * v;
    for (float& v : t) v = float(v / std::sqrt(sum));
    return t;
}

double likeness(const Chroma& unit, const Chroma& shape)
{
    double sum = 0.0;
    for (int c = 0; c < 12; ++c) sum += double(unit[std::size_t(c)]) * shape[std::size_t(c)];
    return sum;
}

// The slots' rows with what never goes away taken out (a drone), each to length 1 (`notes`)
// or to its largest 1 (`bass`).
std::vector<Chroma> withoutDrone(const std::vector<Chroma>& rows, bool unitLength)
{
    std::vector<Chroma> out = rows;
    if (rows.size() >= 8) {
        std::vector<float> column(rows.size());
        for (int c = 0; c < 12; ++c) {
            for (std::size_t i = 0; i < rows.size(); ++i) column[i] = rows[i][std::size_t(c)];
            const auto at = column.begin() + std::ptrdiff_t(DroneQuantile * double(rows.size() - 1));
            std::nth_element(column.begin(), at, column.end());
            const float always = float(DroneTaken) * *at;
            for (auto& row : out) row[std::size_t(c)] = std::max(0.0f, row[std::size_t(c)] - always);
        }
    }
    for (auto& row : out) {
        double scale = 0.0;
        if (unitLength) {
            for (float v : row) scale += double(v) * v;
            scale = std::sqrt(scale);
        } else {
            scale = *std::max_element(row.begin(), row.end());
        }
        if (scale > 0.0)
            for (float& v : row) v = float(v / scale);
    }
    return out;
}

// How well a chord sits in a key: its home chord, one of its own, or a stranger.
double fitInKey(int root, int quality, const chords::Key& key)
{
    const int degree = ((root - key.tonic) % 12 + 12) % 12;
    const bool minorChord = quality == 1 || quality == 4;
    if (degree == 0 && minorChord == key.minor) return 1.0;
    if (!key.minor) {
        // I ii iii IV V vi
        if ((degree == 5 || degree == 7) && !minorChord) return 0.6;
        if ((degree == 2 || degree == 4 || degree == 9) && minorChord) return 0.6;
    } else {
        // i III iv v VI VII, and the major V
        if ((degree == 3 || degree == 8 || degree == 10) && !minorChord) return 0.6;
        if ((degree == 5 || degree == 7) && minorChord) return 0.6;
        if (degree == 7 && !minorChord) return 0.5;
    }
    return -0.4;
}

chords::Key keyOf(const std::vector<int>& states, const std::vector<double>& slotSeconds)
{
    std::array<double, States> held{};
    for (std::size_t i = 0; i < states.size(); ++i) held[std::size_t(states[i])] += slotSeconds[i + 1] - slotSeconds[i];
    int first = 0, last = 0;
    for (int s : states)
        if (s > 0) { if (!first) first = s; last = s; }
    chords::Key best;
    double bestScore = -1e300;
    for (int minor = 0; minor < 2; ++minor)
        for (int tonic = 0; tonic < 12; ++tonic) {
            const chords::Key key{tonic, minor == 1};
            double score = 0.0, all = 0.0;
            for (int s = 1; s < States; ++s) {
                score += held[std::size_t(s)] * fitInKey(rootOf(s), qualityOf(s), key);
                all += held[std::size_t(s)];
            }
            // A song mostly starts and ends at home.
            for (int s : {first, last})
                if (s > 0 && fitInKey(rootOf(s), qualityOf(s), key) == 1.0) score += 0.04 * all;
            if (score > bestScore + 1e-12) { bestScore = score; best = key; }
        }
    return best;
}

struct Decoded {
    std::vector<int> states;
    std::vector<float> sureness;
};

Decoded decode(const ChordAnalysis& a, const ChordOptions& options, const chords::Key* key)
{
    const auto n = a.slotNotes.size();
    Decoded out;
    if (n == 0) return out;
    const auto smoothed = [&](const std::vector<Chroma>& rows) {
        if (Smooth <= 0) return rows;
        std::vector<Chroma> out(rows.size(), Chroma{});
        for (long i = 0; i < long(rows.size()); ++i)
            for (long j = std::max(0L, i - Smooth); j <= std::min(long(rows.size()) - 1, i + Smooth); ++j)
                for (int c = 0; c < 12; ++c) out[std::size_t(i)][std::size_t(c)] += rows[std::size_t(j)][std::size_t(c)];
        return out;
    };
    const auto notes = withoutDrone(smoothed(a.slotNotes), true), bass = withoutDrone(smoothed(a.slotBass), false);
    std::array<Chroma, States> templates{};
    for (int s = 1; s < States; ++s) templates[std::size_t(s)] = templateOf(s);

    // A typical beat's level, to know silence.
    std::vector<float> levels(n);
    for (std::size_t i = 0; i < n; ++i) {
        float sum = 0.0f;
        for (float v : a.slotNotes[i]) sum += v;
        levels[i] = sum;
    }
    std::vector<float> sorted = levels;
    std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(n / 2), sorted.end());
    const float typical = sorted[n / 2];

    std::vector<std::array<double, States>> like(n);
    for (std::size_t i = 0; i < n; ++i) {
        const bool silent = !(typical > 0.0f) || levels[i] < float(SilentBelow) * typical;
        like[i][0] = silent ? 1.0 : 0.3;
        for (int s = 1; s < States; ++s) {
            if (silent || (!options.sevenths && isSeventh(s))) { like[i][std::size_t(s)] = -1.0; continue; }
            double v = likeness(notes[i], templates[std::size_t(s)]) + BassWeight * bass[i][std::size_t(rootOf(s))];
            if (isSeventh(s)) v -= SeventhCost;
            if (key && fitInKey(rootOf(s), qualityOf(s), *key) > 0.0) v += InKeyBonus;
            like[i][std::size_t(s)] = v;
        }
    }

    // The likeliest run: staying is free, a change costs.
    const auto onBar = [&](std::size_t slot) {
        if (!(a.barSeconds > 0.0)) return false;
        const double x = (a.slotSeconds[slot] - a.firstBarSeconds) / a.barSeconds;
        return std::abs(x - std::round(x)) * a.barSeconds < 0.06;
    };
    std::vector<std::array<int, States>> from(n);
    std::array<double, States> score = like[0], next{};
    for (std::size_t i = 1; i < n; ++i) {
        const double cost = onBar(i) ? ChangeCostOnBar : ChangeCost;
        int bestState = 0;
        for (int s = 1; s < States; ++s)
            if (score[std::size_t(s)] > score[std::size_t(bestState)]) bestState = s;
        for (int s = 0; s < States; ++s) {
            const double stay = score[std::size_t(s)], change = score[std::size_t(bestState)] - cost;
            const bool stays = stay >= change;
            from[i][std::size_t(s)] = stays ? s : bestState;
            next[std::size_t(s)] = (stays ? stay : change) + like[i][std::size_t(s)];
        }
        score = next;
    }
    out.states.assign(n, 0);
    int state = 0;
    for (int s = 1; s < States; ++s)
        if (score[std::size_t(s)] > score[std::size_t(state)]) state = s;
    for (std::size_t i = n; i-- > 0;) {
        out.states[i] = state;
        if (i > 0) state = from[i][std::size_t(state)];
    }
    // How sure: the chosen chord against the best chord on another root.
    out.sureness.assign(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const int chosen = out.states[i];
        if (chosen == 0) { out.sureness[i] = 1.0f; continue; }
        double other = -1.0;
        for (int s = 1; s < States; ++s)
            if (rootOf(s) != rootOf(chosen)) other = std::max(other, like[i][std::size_t(s)]);
        out.sureness[i] = float(std::clamp((like[i][std::size_t(chosen)] - other) / 0.25, 0.0, 1.0));
    }
    return out;
}

ChordQuality qualityFor(int state)
{
    if (state == 0) return ChordQuality::None;
    constexpr ChordQuality q[Qualities] = {ChordQuality::Major, ChordQuality::Minor, ChordQuality::Dominant7, ChordQuality::Major7,
                                            ChordQuality::Minor7};
    return q[qualityOf(state)];
}

} // namespace

std::string chordName(int root, ChordQuality quality, const chords::Key& key)
{
    if (quality == ChordQuality::None || root < 0) return {};
    std::string name = chords::noteName(root, chords::usesFlats(key));
    switch (quality) {
    case ChordQuality::Minor: name += "m"; break;
    case ChordQuality::Dominant7: name += "7"; break;
    case ChordQuality::Major7: name += "maj7"; break;
    case ChordQuality::Minor7: name += "m7"; break;
    default: break;
    }
    return name;
}

void decodeChords(ChordAnalysis& a, const ChordOptions& options)
{
    a.chords.clear();
    const auto n = a.slotNotes.size();
    if (n == 0 || a.slotSeconds.size() != n + 1 || a.slotBass.size() != n) return;
    Decoded decoded = decode(a, options, nullptr);
    a.key = keyOf(decoded.states, a.slotSeconds);
    decoded = decode(a, options, &a.key);
    a.key = keyOf(decoded.states, a.slotSeconds);
    for (std::size_t i = 0; i < n; ++i) {
        const int state = decoded.states[i];
        if (!a.chords.empty() && a.chords.back().root == (state ? rootOf(state) : -1) && a.chords.back().quality == qualityFor(state)) {
            auto& span = a.chords.back();
            span.sureness = (span.sureness * float(span.slotCount) + decoded.sureness[i]) / float(span.slotCount + 1);
            ++span.slotCount;
            span.toSeconds = a.slotSeconds[i + 1];
            continue;
        }
        ChordSpan span;
        span.fromSeconds = a.slotSeconds[i];
        span.toSeconds = a.slotSeconds[i + 1];
        span.fromSlot = int(i);
        span.slotCount = 1;
        span.root = state ? rootOf(state) : -1;
        span.quality = qualityFor(state);
        span.sureness = decoded.sureness[i];
        a.chords.push_back(std::move(span));
    }
    for (auto& span : a.chords) span.name = chordName(span.root, span.quality, a.key);
}

std::vector<std::string> commonChords(const ChordAnalysis& a)
{
    std::map<std::string, double> held;
    for (const auto& span : a.chords)
        if (span.root >= 0) held[span.name] += span.toSeconds - span.fromSeconds;
    std::vector<std::pair<std::string, double>> order(held.begin(), held.end());
    std::stable_sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
    std::vector<std::string> names;
    for (const auto& [name, seconds] : order) names.push_back(name);
    return names;
}

std::optional<ChordAnalysis> analyseChords(const std::vector<float>& mono, double sampleRate, const ChordOptions& options,
                                           const Progress& progress)
{
    if (!(sampleRate > 8000.0) || double(mono.size()) < ChordMinSeconds * sampleRate) return std::nullopt;
    ChordAnalysis a;
    a.seconds = double(mono.size()) / sampleRate;
    const Chromagram chroma = chromagram(mono, sampleRate, progress.sub(0.0, 0.45));
    if (progress.cancelled() || chroma.frames == 0) return std::nullopt;
    a.tuningCents = chroma.tuningCents;

    // The beats, as the drum analyzer finds them.
    Beat beat;
    Meter meter;
    {
        const auto spectrogram = bandSpectrogram(mono, sampleRate, progress.sub(0.45, 0.8));
        if (progress.cancelled() || spectrogram.frames == 0) return std::nullopt;
        const auto parts = separate(spectrogram, progress.sub(0.8, 0.97));
        if (progress.cancelled() || parts.percussive.empty()) return std::nullopt;
        Envelopes envelopes = analysis::envelopes(spectrogram, parts.percussive);
        envelopes.change = harmonicChange(spectrogram, parts.harmonic);
        double harmonic = 0.0, whole = 0.0;
        for (std::size_t i = 0; i < parts.harmonic.size(); ++i) { harmonic += parts.harmonic[i]; whole += spectrogram.magnitude[i]; }
        envelopes.harmonicShare = whole > 0.0 ? float(harmonic / whole) : 0.0f;
        beat = trackBeats(envelopes.total, envelopes.frameRate);
        if (beat.strength > 0.0) meter = findMeter(envelopes, beat);
    }
    a.hasBeat = beat.strength > 0.0;
    if (a.hasBeat) {
        const double beatSeconds = beat.period / beat.frameRate;
        a.bpm = beat.bpm() * meter.beatQuarters;
        a.numerator = meter.numerator;
        a.denominator = meter.denominator;
        a.barSeconds = beatSeconds * meter.beatsPerBar;
        a.firstBarSeconds = beat.secondsOf(meter.downbeat);
        a.firstBarSeconds -= std::ceil(a.firstBarSeconds / a.barSeconds - 1e-9) * a.barSeconds;      // the bar line at or before 0
        if (a.firstBarSeconds <= -a.barSeconds + 1e-9) a.firstBarSeconds += a.barSeconds;
        // Slots: the beats, from the one at or before the start.
        double t = beat.secondsOf(0);
        t -= std::ceil(t / beatSeconds - 1e-9) * beatSeconds;
        for (; t < a.seconds; t += beatSeconds) a.slotSeconds.push_back(std::max(0.0, t));
    } else {
        for (double t = 0.0; t < a.seconds; t += 0.5) a.slotSeconds.push_back(t);
    }
    a.slotSeconds.push_back(a.seconds);
    // A first slot of almost no length (the beat just after the start) joins the next.
    if (a.slotSeconds.size() > 2 && a.slotSeconds[1] - a.slotSeconds[0] < 0.1) a.slotSeconds.erase(a.slotSeconds.begin() + 1);
    for (std::size_t i = 0; i + 1 < a.slotSeconds.size(); ++i) {
        // The notes just after a beat still ring from the chord before: the slot's later part counts.
        const double from = a.slotSeconds[i], to = a.slotSeconds[i + 1];
        a.slotNotes.push_back(chromaBetween(chroma.notes, chroma.frameRate, from + 0.15 * (to - from), to));
        a.slotBass.push_back(chromaBetween(chroma.bass, chroma.frameRate, from + 0.15 * (to - from), to));
    }
    decodeChords(a, options);
    progress.at(1.0);
    return a;
}

} // namespace analysis
