// drumbench: measures the drum pattern analyzer (restMap G20) on real recordings, stage by
// stage, against the MIDI the drummer played. Not part of the app.
//
//   drumbench <audio> [<reference.mid>] [--tol <ms>] [--grid-tol <ms>] [--dump]
//   drumbench --dir <folder> [--tol <ms>] [--grid-tol <ms>]
//
// <audio> is a WAV, FLAC or MP3; it is mixed to mono, as the app does. Without a reference the
// analysis is only printed. --dir takes every audio file in the folder (and below) that has a
// .mid / .midi of the same name beside it, prints one line per file and the totals.
//
// --dump also lists every played moment with each drum's rise there (to see why a hit was
// missed or read as another drum).
//
// --hits-mid <out.mid> (one file) writes the found hits, tempo and meter as a MIDI file: the
// analysis of a song's drum stem then serves as the reference for the whole song.
//
// With a reference, per stage:
//   tempo    the found BPM against the MIDI's (also told: double, half, 3:2, 2:3)
//   beats    how far the found beats lie from the MIDI's quarter-note grid (when the tempo fits)
//   meter    the found time signature against the MIDI's
//   hits     the hits before the grid, per drum, within --tol (30 ms): found / missed / extra,
//            and which other drum an extra hit really was (a crash read as a hat, ...)
//   pattern  the notes after the grid, per drum, within --grid-tol (60 ms)
//
// The reference's notes are read as General MIDI / Roland TD drums (the Groove MIDI Dataset's):
// kick 35 36; snare 37 38 40; hi-hat 22 26 42 44 46; toms 41 43 45 47 48 50 58; cymbals (crash,
// ride, splash, china) 49 51 52 53 55 57 59. The pedal hi-hat (44) may be found as a hat but
// need not be.
//
// The analyzer writes only kick, snare and hi-hat (the keys both kits share), so a ride or a
// crash is asked for as a hi-hat: the reference's cymbals count as hats ("hat" below is "hat or
// cymbal"). Toms have no key: a tom read as anything is an extra. A played note softer than
// velocity 40 is a ghost note: counted apart ("soft"), as missing those matters less.

#include "analysis/drumtranscriber.h"
#include "analysis/hpss.h"
#include "analysis/kitshapes.h"
#include "analysis/spectrogram.h"
#include "audiofile/audioimport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

enum Class { Kick = 0, Snare, Hat, Tom, Cymbal, ClassCount, Ignored };
const char* const ClassName[ClassCount] = {"kick", "snare", "hat", "tom", "cymbal"};

Class classOfGm(int note)
{
    switch (note) {
    case 35: case 36: return Kick;
    case 37: case 38: case 40: return Snare;
    case 22: case 26: case 42: case 46: return Hat;
    case 44: return Hat;                                   // the pedal: may be found, need not be
    case 41: case 43: case 45: case 47: case 48: case 50: case 58: return Tom;
    case 49: case 51: case 52: case 53: case 55: case 57: case 59: return Hat;   // see the top
    default: return Ignored;
    }
}

Class classOfKey(int key)
{
    if (key == analysis::KickKey) return Kick;
    if (key == analysis::SnareKey) return Snare;
    return Hat;
}

struct Event {
    double seconds = 0.0;
    Class drum = Kick;
    bool soft = false;                                     // a ghost note (played only)
    bool optional = false;                                 // found or not, neither right nor wrong
};

struct Reference {
    std::vector<Event> events;                             // in time order
    double bpm = 0.0;                                      // the first tempo (0: none written)
    bool tempoChanges = false;
    int numerator = 0, denominator = 0;                    // the first time signature (0: none)
};

// ---------------------------------------------------------------- the MIDI file

struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t at = 0, end = 0;

    std::uint8_t byte()
    {
        if (at >= end) throw std::runtime_error("the MIDI file ends too early");
        return bytes[at++];
    }
    std::uint32_t number(int count)
    {
        std::uint32_t v = 0;
        for (int i = 0; i < count; ++i) v = (v << 8) | byte();
        return v;
    }
    std::uint32_t variable()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const std::uint8_t b = byte();
            v = (v << 7) | (b & 0x7Fu);
            if (!(b & 0x80u)) break;
        }
        return v;
    }
};

Reference readMidi(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Reader r{bytes, 0, bytes.size()};
    if (bytes.size() < 14 || std::memcmp(bytes.data(), "MThd", 4) != 0) throw std::runtime_error(path + " is not a MIDI file");
    r.at = 4;
    const std::uint32_t headerLength = r.number(4);
    r.number(2);
    const int tracks = int(r.number(2));
    const int division = int(r.number(2));
    if (division & 0x8000) throw std::runtime_error(path + ": SMPTE time is not read");
    r.at = 8 + headerLength;

    struct Tempo { std::uint64_t tick; double microsPerQuarter; };
    struct Note { std::uint64_t tick; int note; int velocity; };
    std::vector<Tempo> tempos;
    std::vector<Note> notes;
    Reference ref;
    std::uint64_t firstSignatureTick = ~0ull;

    for (int t = 0; t < tracks && r.at + 8 <= bytes.size(); ++t) {
        r.end = bytes.size();
        const bool isTrack = std::memcmp(bytes.data() + r.at, "MTrk", 4) == 0;
        r.at += 4;
        const std::uint32_t length = r.number(4);
        const std::size_t stop = std::min(bytes.size(), r.at + length);
        if (!isTrack) { r.at = stop; --t; continue; }
        r.end = stop;
        std::uint64_t tick = 0;
        std::uint8_t status = 0;
        while (r.at < stop) {
            tick += r.variable();
            std::uint8_t b = r.byte();
            if (b == 0xFF) {
                const std::uint8_t type = r.byte();
                const std::uint32_t n = r.variable();
                const std::size_t next = std::min(stop, r.at + n);
                if (type == 0x51 && n == 3) tempos.push_back({tick, double(r.number(3))});
                else if (type == 0x58 && n >= 2 && tick < firstSignatureTick) {
                    firstSignatureTick = tick;
                    ref.numerator = r.byte();
                    ref.denominator = 1 << r.byte();
                }
                r.at = next;
                continue;
            }
            if (b == 0xF0 || b == 0xF7) { r.at = std::min(stop, r.at + r.variable()); continue; }
            int data1;
            if (b & 0x80) { status = b; data1 = r.byte(); }
            else data1 = b;                                 // running status
            const int kind = status & 0xF0;
            if (kind == 0xC0 || kind == 0xD0) continue;      // one data byte
            const int data2 = r.byte();
            if (kind == 0x90 && data2 > 0) notes.push_back({tick, data1, data2});
        }
        r.at = stop;
    }

    std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
    if (!tempos.empty()) {
        ref.bpm = 60e6 / tempos.front().microsPerQuarter;
        for (const auto& t : tempos)
            if (std::abs(t.microsPerQuarter - tempos.front().microsPerQuarter) > 1.0) ref.tempoChanges = true;
    }
    const auto secondsOf = [&](std::uint64_t tick) {
        double seconds = 0.0, micros = 500000.0;
        std::uint64_t from = 0;
        for (const auto& t : tempos) {
            if (t.tick >= tick) break;
            seconds += double(t.tick - from) * micros / (1e6 * division);
            from = t.tick;
            micros = t.microsPerQuarter;
        }
        return seconds + double(tick - from) * micros / (1e6 * division);
    };
    for (const auto& n : notes) {
        const Class c = classOfGm(n.note);
        if (c != Ignored) ref.events.push_back({secondsOf(n.tick), c, n.velocity < 40, n.note == 44});
    }
    std::stable_sort(ref.events.begin(), ref.events.end(), [](const Event& a, const Event& b) { return a.seconds < b.seconds; });
    return ref;
}

// ---------------------------------------------------------------- the scores

struct Score {
    int found = 0, missed = 0, extra = 0;
    int soft = 0, softMissed = 0;                           // of the played: ghost notes, and those missed
    int extraWas[ClassCount] = {};                          // an extra hit that another drum played
    double errorSum = 0.0;                                  // |found - played| seconds, of the found

    void add(const Score& o)
    {
        found += o.found; missed += o.missed; extra += o.extra; errorSum += o.errorSum;
        soft += o.soft; softMissed += o.softMissed;
        for (int c = 0; c < ClassCount; ++c) extraWas[c] += o.extraWas[c];
    }
    double precision() const { return found + extra > 0 ? double(found) / (found + extra) : 0.0; }
    double recall() const { return found + missed > 0 ? double(found) / (found + missed) : 0.0; }
    double f() const { return 2 * found + missed + extra > 0 ? 2.0 * found / (2 * found + missed + extra) : 0.0; }
};

// `found` against `played` for one drum, each played hit taken once, the nearest first in time.
Score score(const std::vector<Event>& found, const std::vector<Event>& played, Class drum, double tolerance)
{
    std::vector<double> a, b;
    std::vector<bool> soft, optional;
    for (const auto& e : found) if (e.drum == drum) a.push_back(e.seconds);
    for (const auto& e : played) if (e.drum == drum) { b.push_back(e.seconds); soft.push_back(e.soft); optional.push_back(e.optional); }
    Score s;
    std::vector<bool> taken(b.size(), false);
    std::size_t from = 0;
    for (double t : a) {
        while (from < b.size() && b[from] < t - tolerance) ++from;
        std::size_t best = b.size();
        for (std::size_t j = from; j < b.size() && b[j] <= t + tolerance; ++j)
            if (!taken[j] && (best == b.size() || std::abs(b[j] - t) < std::abs(b[best] - t))) best = j;
        if (best < b.size()) {
            taken[best] = true;
            if (optional[best]) continue;
            ++s.found;
            s.errorSum += std::abs(b[best] - t);
            continue;
        }
        ++s.extra;
        // What was played there instead: the nearest hit of another drum.
        int was = -1;
        double nearest = tolerance;
        for (const auto& e : played)
            if (e.drum != drum && std::abs(e.seconds - t) <= nearest) { nearest = std::abs(e.seconds - t); was = e.drum; }
        if (was >= 0) ++s.extraWas[was];
    }
    for (std::size_t j = 0; j < b.size(); ++j) {
        if (optional[j]) continue;
        if (!taken[j]) ++s.missed;
        if (soft[j]) { ++s.soft; if (!taken[j]) ++s.softMissed; }
    }
    return s;
}

struct FileResult {
    bool analysed = false;
    double seconds = 0.0;
    double bpm = 0.0, refBpm = 0.0;
    const char* tempoVerdict = "?";
    bool tempoRight = false;
    bool meterKnown = false, meterRight = false;
    double beatOffsetMs = -1.0;                             // mean distance of the beats to the grid
    Score hits[3], pattern[3];
    int played[ClassCount] = {};
};

const char* tempoVerdictOf(double found, double ref, bool& right)
{
    right = false;
    if (!(ref > 0.0)) return "no reference";
    const auto near = [&](double factor) { return std::abs(found / (ref * factor) - 1.0) < 0.03; };
    if (near(1.0)) { right = true; return "right"; }
    if (near(2.0)) return "DOUBLE";
    if (near(0.5)) return "HALF";
    if (near(1.5)) return "3:2";
    if (near(2.0 / 3.0)) return "2:3";
    if (near(4.0 / 3.0)) return "4:3";
    if (near(0.75)) return "3:4";
    return "WRONG";
}

// The found hits as a MIDI file in seconds-true time at the found tempo and meter.
void writeHitsMidi(const std::string& path, const analysis::DrumAnalysis& a)
{
    const auto variable = [](std::vector<std::uint8_t>& out, std::uint32_t v) {
        std::uint8_t bytes[5];
        int n = 0;
        bytes[n++] = v & 0x7Fu;
        while (v >>= 7) bytes[n++] = 0x80u | (v & 0x7Fu);
        while (n > 0) out.push_back(bytes[--n]);
    };
    const double bpm = a.pattern.bpm > 0.0 ? a.pattern.bpm : 120.0;
    const auto micros = std::uint32_t(std::lround(60e6 / bpm));
    int power = 0;
    while ((1 << power) < a.pattern.denominator) ++power;
    std::vector<std::uint8_t> track = {0x00, 0xFF, 0x51, 0x03, std::uint8_t(micros >> 16), std::uint8_t(micros >> 8), std::uint8_t(micros),
                                       0x00, 0xFF, 0x58, 0x04, std::uint8_t(a.pattern.numerator), std::uint8_t(power), 24, 8};
    std::uint32_t last = 0;
    for (const auto& h : a.hits) {
        const auto tick = std::uint32_t(std::lround(std::max(0.0, h.seconds) * 1e6 / micros * 480.0));
        variable(track, tick - std::min(tick, last));
        last = std::max(last, tick);
        const int note = h.key == analysis::KickKey ? 36 : h.key == analysis::SnareKey ? 38 : 42;
        track.insert(track.end(), {0x99, std::uint8_t(note), std::uint8_t(std::clamp(int(40 + 60 * h.strength), 40, 127))});
    }
    track.insert(track.end(), {0x00, 0xFF, 0x2F, 0x00});
    std::ofstream out(path, std::ios::binary);
    const std::uint8_t head[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k',
                                 std::uint8_t(track.size() >> 24), std::uint8_t(track.size() >> 16), std::uint8_t(track.size() >> 8), std::uint8_t(track.size())};
    out.write(reinterpret_cast<const char*>(head), sizeof head);
    out.write(reinterpret_cast<const char*>(track.data()), std::streamsize(track.size()));
    if (!out) throw std::runtime_error("cannot write " + path);
}

std::vector<float> monoOf(const AudioData& audio)
{
    std::vector<float> mono(std::size_t(std::max<std::int64_t>(0, audio.frames)), 0.0f);
    if (audio.channels.empty()) return mono;
    for (const auto& channel : audio.channels)
        for (std::size_t i = 0; i < mono.size() && i < channel.size(); ++i) mono[i] += channel[i];
    const float scale = 1.0f / float(audio.channels.size());
    for (float& v : mono) v *= scale;
    return mono;
}

void printScore(const char* name, const Score& s, int played)
{
    std::printf("    %-6s played %4d   found %4d   missed %4d   extra %4d   precision %3.0f%%  recall %3.0f%%  F %3.0f%%",
                name, played, s.found, s.missed, s.extra, 100.0 * s.precision(), 100.0 * s.recall(), 100.0 * s.f());
    if (s.found > 0) std::printf("  off %2.0f ms", 1000.0 * s.errorSum / s.found);
    if (s.soft > 0) std::printf("   soft: %d of %d missed", s.softMissed, s.soft);
    bool any = false;
    for (int c = 0; c < ClassCount; ++c)
        if (s.extraWas[c] > 0) {
            std::printf("%s%d %s", any ? ", " : "   extras were: ", s.extraWas[c], ClassName[c]);
            any = true;
        }
    std::printf("\n");
}

FileResult run(const std::string& audioPath, const std::string& midiPath, double tolerance, double gridTolerance, bool verbose, bool dump = false,
               const std::string& hitsMidi = {})
{
    FileResult out;
    const auto audio = audioimport::read(audioPath);
    const auto mono = monoOf(*audio);
    out.seconds = double(mono.size()) / audio->sampleRate;
    const auto analysis = analysis::analyseDrums(mono, audio->sampleRate);
    if (!analysis) {
        if (verbose) std::printf("%s\n  nothing found (too short, or silent)\n", audioPath.c_str());
        return out;
    }
    out.analysed = true;
    const auto& a = *analysis;
    const auto& p = a.pattern;
    out.bpm = p.bpm;
    if (verbose) {
        std::printf("%s\n", audioPath.c_str());
        std::printf("  %.1f s at %.0f Hz, %d channel(s); harmonic share %.2f\n", out.seconds, audio->sampleRate,
                    int(audio->channels.size()), double(a.envelopes.harmonicShare));
        std::printf("  found    %.2f BPM, %d/%d, %d bars from %.3f s, grid 1/%.0f; beat fit %.3f\n", p.bpm, p.numerator,
                    p.denominator, p.bars, p.firstBarSeconds, 4.0 / p.gridQuarters, a.beat.strength);
        int raw[3] = {};
        for (const auto& h : a.hits) ++raw[classOfKey(h.key)];
        std::printf("  hits     kick %d, snare %d, hat %d before the grid; %d, %d, %d after it\n", raw[0], raw[1], raw[2],
                    p.kicks, p.snares, p.hats);
    }
    if (!hitsMidi.empty()) writeHitsMidi(hitsMidi, a);
    if (midiPath.empty()) return out;

    const Reference ref = readMidi(midiPath);
    for (const auto& e : ref.events)
        if (!e.optional) ++out.played[e.drum];
    out.refBpm = ref.bpm;
    out.tempoVerdict = tempoVerdictOf(p.bpm, ref.bpm, out.tempoRight);
    out.meterKnown = ref.numerator > 0;
    out.meterRight = out.meterKnown && ref.numerator == p.numerator && ref.denominator == p.denominator;
    if (out.tempoRight && !ref.tempoChanges) {
        // The found beats against the MIDI's quarter notes (which start at 0 s).
        const double quarter = 60.0 / ref.bpm;
        double sum = 0.0;
        int count = 0;
        for (int k = 0; a.beat.secondsOf(k) < out.seconds; ++k) {
            const double x = a.beat.secondsOf(k) / (quarter * a.meter.beatQuarters);
            sum += std::abs(x - std::round(x)) * quarter * a.meter.beatQuarters;
            ++count;
        }
        if (count > 0) out.beatOffsetMs = 1000.0 * sum / count;
    }
    if (dump) {
        // Each played moment (hits within 15 ms as one): who played, and each drum's rise there
        // against its typical hit (what pickHits compares with its thresholds).
        const auto& e = a.envelopes;
        const auto spec = analysis::bandSpectrogram(mono, audio->sampleRate);
        const auto parts = analysis::separate(spec);
        const double edges[9] = {0.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 1e9};
        {
            std::vector<float> cells(parts.percussive.size());
            for (std::size_t i = 0; i < cells.size(); ++i) cells[i] = std::pow(parts.percussive[i], analysis::Compression);
            const auto kit = analysis::learnKit(spec, cells);
            std::printf("  the kit learnt from %d hits (shares in the bands below):\n", kit.hits);
            const char* const roles[5] = {"mix", "KICK", "SNARE", "HAT", "other"};
            for (const auto& g : kit.groups) {
                double share[8] = {};
                for (int b = 0; b < spec.bands; ++b) {
                    int k = 0;
                    while (k < 7 && spec.centreHz[b] >= edges[k + 1]) ++k;
                    share[k] += g.centre[std::size_t(b)];
                }
                std::printf("    [");
                for (int k = 0; k < 8; ++k) std::printf("%3.0f", 100.0 * share[k]);
                std::printf(" ] %3d hits, level %7.3g  %s\n", g.count, g.level, roles[g.role + 1]);
            }
        }
        std::printf("  shares of the hit's rise in: <100 | 100-200 | 200-500 | 500-1k | 1k-2k | 2k-4k | 4k-8k | >8k Hz\n");
        const float typical[3] = {analysis::percentile(e.kick, 0.99), analysis::percentile(e.snare, 0.99), analysis::percentile(e.hat, 0.99)};
        std::printf("  typical rises: kick %.4g  snare %.4g  hat %.4g\n", double(typical[0]), double(typical[1]), double(typical[2]));
        for (std::size_t i = 0; i < ref.events.size();) {
            std::string who;
            std::size_t j = i;
            for (; j < ref.events.size() && ref.events[j].seconds - ref.events[i].seconds < 0.015; ++j) {
                who += ClassName[ref.events[j].drum];
                who += ref.events[j].soft ? "~ " : " ";
            }
            const double frame = ref.events[i].seconds * e.frameRate;
            {
                const int f = int(std::lround(frame));
                double share[8] = {}, all = 0.0;
                for (int b = 0; b < spec.bands; ++b) {
                    float top = 0.0f;
                    for (int g = f; g <= f + 2; ++g)
                        if (g >= 0 && g < spec.frames) top = std::max(top, parts.percussive[std::size_t(g) * spec.bands + b]);
                    const float before = f - 2 >= 0 && f - 2 < spec.frames ? parts.percussive[std::size_t(f - 2) * spec.bands + b] : 0.0f;
                    const double rise = std::max(0.0f, top - before);
                    int k = 0;
                    while (k < 7 && spec.centreHz[b] >= edges[k + 1]) ++k;
                    share[k] += rise;
                    all += rise;
                }
                std::printf("  [");
                for (int k = 0; k < 8; ++k) std::printf("%3.0f", all > 0.0 ? 100.0 * share[k] / all : 0.0);
                std::printf(" ] %8.3g", all);
            }
            std::printf("  %7.3f  %-22s kick %5.2f  snare %5.2f  hat %5.2f\n", ref.events[i].seconds, who.c_str(),
                        double(analysis::peakNear(e.kick, frame, 3) / typical[0]), double(analysis::peakNear(e.snare, frame, 3) / typical[1]),
                        double(analysis::peakNear(e.hat, frame, 3) / typical[2]));
            i = j;
        }
    }
    std::vector<Event> hits, notes;
    for (const auto& h : a.hits) hits.push_back({h.seconds, classOfKey(h.key)});
    for (const auto& n : p.notes) notes.push_back({p.firstBarSeconds + n.startBeat * p.secondsPerQuarter, classOfKey(n.note)});
    std::sort(notes.begin(), notes.end(), [](const Event& x, const Event& y) { return x.seconds < y.seconds; });
    for (int d = 0; d < 3; ++d) {
        out.hits[d] = score(hits, ref.events, Class(d), tolerance);
        out.pattern[d] = score(notes, ref.events, Class(d), gridTolerance);
    }
    if (verbose) {
        std::printf("  played   %.2f BPM%s, %d/%d; kick %d, snare %d, hat %d, tom %d, cymbal %d\n", ref.bpm,
                    ref.tempoChanges ? " (changes)" : "", ref.numerator, ref.denominator, out.played[Kick], out.played[Snare],
                    out.played[Hat], out.played[Tom], out.played[Cymbal]);
        std::printf("  tempo    %s\n", out.tempoVerdict);
        if (out.beatOffsetMs >= 0.0) std::printf("  beats    %.0f ms from the played grid on average\n", out.beatOffsetMs);
        if (out.meterKnown) std::printf("  meter    %s\n", out.meterRight ? "right" : "WRONG");
        std::printf("  hits before the grid (within %.0f ms):\n", 1000.0 * tolerance);
        for (int d = 0; d < 3; ++d) printScore(ClassName[d], out.hits[d], out.played[d]);
        std::printf("  pattern after the grid (within %.0f ms):\n", 1000.0 * gridTolerance);
        for (int d = 0; d < 3; ++d) printScore(ClassName[d], out.pattern[d], out.played[d]);
    }
    return out;
}

bool isAudio(const std::filesystem::path& path)
{
    std::string e = path.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e == ".wav" || e == ".flac" || e == ".mp3";
}

int runFolder(const std::string& folder, double tolerance, double gridTolerance)
{
    namespace fs = std::filesystem;
    std::vector<std::pair<std::string, std::string>> pairs;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file() || !isAudio(entry.path())) continue;
        for (const char* ext : {".mid", ".midi"}) {
            auto midi = entry.path();
            midi.replace_extension(ext);
            if (fs::exists(midi)) { pairs.push_back({entry.path().string(), midi.string()}); break; }
        }
    }
    std::sort(pairs.begin(), pairs.end());
    if (pairs.empty()) { std::fprintf(stderr, "no audio file with a .mid beside it in %s\n", folder.c_str()); return 1; }

    std::printf("%-44s %7s %7s %-6s %5s %5s | hits F%%: %4s %5s %4s | pattern F%%: %4s %5s %4s\n", "file", "found", "played",
                "tempo", "beats", "meter", "kick", "snare", "hat", "kick", "snare", "hat");
    Score hits[3], pattern[3];
    int files = 0, tempoRight = 0, meterRight = 0, meterKnown = 0, failed = 0;
    int tempoKinds[8] = {};
    const char* const kinds[8] = {"right", "DOUBLE", "HALF", "3:2", "2:3", "4:3", "3:4", "WRONG"};
    for (const auto& [audio, midi] : pairs) {
        std::string name = std::filesystem::path(audio).filename().string();
        if (name.size() > 44) name = name.substr(0, 41) + "...";
        FileResult r;
        try {
            r = run(audio, midi, tolerance, gridTolerance, false);
        } catch (const std::exception& e) {
            std::printf("%-44s %s\n", name.c_str(), e.what());
            ++failed;
            continue;
        }
        if (!r.analysed) { std::printf("%-44s nothing found\n", name.c_str()); ++failed; continue; }
        ++files;
        tempoRight += r.tempoRight;
        meterKnown += r.meterKnown;
        meterRight += r.meterRight;
        for (int k = 0; k < 8; ++k)
            if (std::strcmp(kinds[k], r.tempoVerdict) == 0) ++tempoKinds[k];
        for (int d = 0; d < 3; ++d) { hits[d].add(r.hits[d]); pattern[d].add(r.pattern[d]); }
        char beats[16] = "    -";
        if (r.beatOffsetMs >= 0.0) std::snprintf(beats, sizeof beats, "%3.0fms", r.beatOffsetMs);
        std::printf("%-44s %7.2f %7.2f %-6s %5s %5s |          %4.0f %5.0f %4.0f |             %4.0f %5.0f %4.0f\n", name.c_str(),
                    r.bpm, r.refBpm, r.tempoVerdict, beats, !r.meterKnown ? "-" : r.meterRight ? "right" : "WRONG",
                    100.0 * r.hits[0].f(), 100.0 * r.hits[1].f(), 100.0 * r.hits[2].f(), 100.0 * r.pattern[0].f(),
                    100.0 * r.pattern[1].f(), 100.0 * r.pattern[2].f());
        std::fflush(stdout);
    }
    std::printf("\n%d file(s)%s\n", files, failed ? " (some could not be analysed, see above)" : "");
    if (files == 0) return 1;
    std::printf("  tempo right in %d of %d;", tempoRight, files);
    for (int k = 1; k < 8; ++k)
        if (tempoKinds[k] > 0) std::printf(" %s %d", kinds[k], tempoKinds[k]);
    std::printf("\n  meter right in %d of %d\n", meterRight, meterKnown);
    std::printf("  hits before the grid (within %.0f ms), all files together:\n", 1000.0 * tolerance);
    for (int d = 0; d < 3; ++d) printScore(ClassName[d], hits[d], hits[d].found + hits[d].missed);
    std::printf("  pattern after the grid (within %.0f ms), all files together:\n", 1000.0 * gridTolerance);
    for (int d = 0; d < 3; ++d) printScore(ClassName[d], pattern[d], pattern[d].found + pattern[d].missed);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> files;
    std::string folder;
    double tolerance = 0.030, gridTolerance = 0.060;
    bool dump = false;
    std::string hitsMidi;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--dir" && i + 1 < argc) folder = argv[++i];
            else if (arg == "--tol" && i + 1 < argc) tolerance = std::stod(argv[++i]) / 1000.0;
            else if (arg == "--grid-tol" && i + 1 < argc) gridTolerance = std::stod(argv[++i]) / 1000.0;
            else if (arg == "--dump") dump = true;
            else if (arg == "--hits-mid" && i + 1 < argc) hitsMidi = argv[++i];
            else if (arg.rfind("--", 0) == 0) throw std::runtime_error("unknown option " + arg);
            else files.push_back(arg);
        }
        if (!folder.empty() && files.empty()) return runFolder(folder, tolerance, gridTolerance);
        if (folder.empty() && (files.size() == 1 || files.size() == 2)) {
            run(files[0], files.size() == 2 ? files[1] : std::string(), tolerance, gridTolerance, true, dump, hitsMidi);
            return 0;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "drumbench: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "usage: drumbench <audio> [<reference.mid>] [--tol <ms>] [--grid-tol <ms>]\n"
                         "       drumbench --dir <folder> [--tol <ms>] [--grid-tol <ms>]\n");
    return 2;
}
