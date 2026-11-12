#pragma once
// Test sounds for the analyzers (restMap G20-G22): known drum patterns played by the app's own
// kits, alone or in a "song" with the Synth's chords and the Bass Guitar's line, so a test
// knows exactly where every hit is.

#include "instruments/bass/bassguitar.h"
#include "instruments/drums/drumkit.h"
#include "instruments/drums/drumkitlibrary.h"
#include "instruments/electrodrums/electrodrums.h"
#include "instruments/synth/synth.h"
#include "instruments/synth/synthpresets.h"
#include "processor/processor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace drumrender {

constexpr double Rate = 48000.0;

struct Hit { double beat; int key; int velocity = 100; };   // beat: quarter notes

struct Groove {
    int numerator = 4, denominator = 4;
    std::vector<Hit> bar;                                      // one bar, repeated
    double barQuarters() const { return numerator * 4.0 / denominator; }
};

constexpr int Kick = 60, Snare = 62, Hat = 66, OpenHat = 70;

// Rock 4/4: kick 1 and 3 (and the "and" of 3), snare 2 and 4, closed hats in eighths.
inline Groove rock()
{
    Groove g;
    for (int i = 0; i < 8; ++i) g.bar.push_back({i * 0.5, Hat, i % 2 ? 70 : 95});
    g.bar.push_back({0.0, Kick, 115});
    g.bar.push_back({2.0, Kick, 105});
    g.bar.push_back({2.5, Kick, 95});
    g.bar.push_back({1.0, Snare, 110});
    g.bar.push_back({3.0, Snare, 110});
    return g;
}

// Waltz 3/4: kick on 1, snare on 2 and 3, hats in eighths.
inline Groove waltz()
{
    Groove g;
    g.numerator = 3;
    for (int i = 0; i < 6; ++i) g.bar.push_back({i * 0.5, Hat, i % 2 ? 70 : 90});
    g.bar.push_back({0.0, Kick, 115});
    g.bar.push_back({1.0, Snare, 95});
    g.bar.push_back({2.0, Snare, 95});
    return g;
}

// 6/8: kick on 1, snare on 4, hats on every eighth.
inline Groove sixEight()
{
    Groove g;
    g.numerator = 6;
    g.denominator = 8;
    for (int i = 0; i < 6; ++i) g.bar.push_back({i * 0.5, Hat, i % 3 ? 70 : 95});
    g.bar.push_back({0.0, Kick, 115});
    g.bar.push_back({1.5, Snare, 110});
    return g;
}

// The groove's hits over `bars` bars from `startSeconds`, in seconds.
struct Timed { double seconds; int key; int velocity; };
inline std::vector<Timed> timed(const Groove& g, double bpm, int bars, double startSeconds)
{
    std::vector<Timed> out;
    const double quarter = 60.0 / bpm;
    for (int b = 0; b < bars; ++b)
        for (const auto& h : g.bar)
            out.push_back({startSeconds + (b * g.barQuarters() + h.beat) * quarter, h.key, h.velocity});
    std::sort(out.begin(), out.end(), [](const Timed& a, const Timed& b) { return a.seconds < b.seconds; });
    return out;
}

struct Event { std::int64_t sample; std::uint8_t status, key, velocity; };

// Plays `events` through the processor into a mono buffer of `seconds`.
inline std::vector<float> render(Processor& p, std::vector<Event> events, double seconds)
{
    constexpr int Block = 256;
    p.prepare(Rate, Block);
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.sample < b.sample; });
    const auto total = std::int64_t(seconds * Rate);
    std::vector<float> out(std::size_t(total), 0.0f), l(Block), r(Block);
    MidiBuffer midi;
    std::size_t next = 0;
    for (std::int64_t at = 0; at < total; at += Block) {
        const int n = int(std::min<std::int64_t>(Block, total - at));
        midi.clear();
        while (next < events.size() && events[next].sample < at + n) {
            const auto& e = events[next++];
            midi.add({int(std::max<std::int64_t>(0, e.sample - at)), e.status, e.key, e.velocity});
        }
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        float* ch[2] = {l.data(), r.data()};
        ProcessContext c;
        c.audio = {ch, 2, n};
        c.midi = &midi;
        c.sampleRate = Rate;
        c.samplePosition = at;
        p.process(c);
        for (int i = 0; i < n; ++i) out[std::size_t(at + i)] = 0.5f * (l[i] + r[i]);
    }
    return out;
}

inline std::vector<Event> drumEvents(const std::vector<Timed>& hits)
{
    std::vector<Event> events;
    for (const auto& h : hits) {
        const auto s = std::int64_t(std::llround(h.seconds * Rate));
        events.push_back({s, 0x90, std::uint8_t(h.key), std::uint8_t(h.velocity)});
        events.push_back({s + 2400, 0x80, std::uint8_t(h.key), 0});
    }
    return events;
}

inline std::vector<float> electroDrums(const std::vector<Timed>& hits, double seconds)
{
    ElectroDrums kit;
    return render(kit, drumEvents(hits), seconds);
}

// The acoustic kit from the repository's sample-kits (empty when it is not there).
inline std::filesystem::path acousticKitFolder()
{
    const auto folder = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
                      / "sample-kits" / drumkit::KitFolderName;
    return std::filesystem::exists(folder) ? folder : std::filesystem::path();
}

inline std::vector<float> acousticDrums(const std::vector<Timed>& hits, double seconds)
{
    const auto folder = acousticKitFolder();
    if (folder.empty()) return {};
    auto library = std::make_shared<drumkit::Library>(folder.string());
    DrumKit kit(library);
    kit.prepare(Rate, 256);
    library->waitUntilIdle();
    return render(kit, drumEvents(hits), seconds);
}

// A song's other parts over the drums: a pad on C - Am - F - G (a bar each) and the bass on the
// chord roots in eighth notes, both in time with the drums.
inline std::vector<float> band(double bpm, int bars, double barQuarters, double startSeconds, double seconds)
{
    const double quarter = 60.0 / bpm;
    const int roots[4] = {36, 33, 29, 31};                     // C2 A1 F1 G1
    const int chords[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};
    std::vector<Event> pad, bass;
    for (int b = 0; b < bars; ++b) {
        const double at = startSeconds + b * barQuarters * quarter;
        const auto from = std::int64_t(at * Rate), to = std::int64_t((at + barQuarters * quarter) * Rate) - 200;
        for (int key : chords[b % 4]) {
            pad.push_back({from, 0x90, std::uint8_t(key), 80});
            pad.push_back({to, 0x80, std::uint8_t(key), 0});
        }
        for (double e = 0.0; e < barQuarters - 1e-9; e += 0.5) {
            const auto s = std::int64_t((at + e * quarter) * Rate);
            bass.push_back({s, 0x90, std::uint8_t(roots[b % 4] + 12), 90});
            bass.push_back({s + std::int64_t(0.42 * quarter * Rate), 0x80, std::uint8_t(roots[b % 4] + 12), 0});
        }
    }
    Synth synth;
    for (const auto& p : synth::factoryPresets())
        if (p.name == "Warm Pad") synth.setSettings(p.settings);
    auto padAudio = render(synth, pad, seconds);
    BassGuitar bassGuitar;
    auto bassAudio = render(bassGuitar, bass, seconds);
    for (std::size_t i = 0; i < padAudio.size(); ++i) padAudio[i] = 0.7f * padAudio[i] + 0.9f * bassAudio[i];
    return padAudio;
}

inline void mixInto(std::vector<float>& into, const std::vector<float>& add, float gain)
{
    for (std::size_t i = 0; i < std::min(into.size(), add.size()); ++i) into[i] += gain * add[i];
}

} // namespace drumrender
