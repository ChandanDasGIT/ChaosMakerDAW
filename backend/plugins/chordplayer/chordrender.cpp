#include "plugins/chordplayer/chordrender.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace chords {

std::vector<MidiNote> renderClip(const Settings& settings, const std::vector<MidiNote>& clipNotes,
                                 double clipStartBeat, double clipLengthBeats,
                                 double bpm, int numerator, int denominator)
{
    std::vector<MidiNote> out;
    if (!(clipLengthBeats > 0) || !(bpm > 0) || !std::isfinite(clipStartBeat)) return out;
    constexpr double Rate = 48000.0;
    constexpr int Block = 256;
    const double samplesPerBeat = 60.0 / bpm * Rate;
    const auto sampleOf = [&](double beat) { return std::int64_t(std::llround(beat * samplesPerBeat)); };

    // The clip's own events in time order (offs before ons at the same sample).
    struct Ev { std::int64_t sample; MidiEvent event; };
    std::vector<Ev> events;
    for (const auto& n : clipNotes) {
        if (n.startBeat < 0 || n.startBeat >= clipLengthBeats || n.note < 0 || n.note > 127) continue;
        const auto on = sampleOf(n.startBeat), off = sampleOf(std::min(n.startBeat + n.lengthBeats, clipLengthBeats));
        events.push_back({on, MidiEvent{0, 0x90, std::uint8_t(n.note), std::uint8_t(std::clamp(n.velocity, 1, 127))}});
        events.push_back({std::max(off, on + 1), MidiEvent{0, 0x80, std::uint8_t(n.note), 0}});
    }
    std::stable_sort(events.begin(), events.end(), [](const Ev& a, const Ev& b) {
        if (a.sample != b.sample) return a.sample < b.sample;
        return (a.event.status & 0xF0) == 0x80 && (b.event.status & 0xF0) != 0x80;
    });

    ChordPlayer player;
    player.prepare(Rate);
    player.setStrips(playerStrips(settings));
    player.setAutoplay(settings.autoplay);
    const std::int64_t origin = sampleOf(clipStartBeat);   // song position of the clip's start
    const std::int64_t length = sampleOf(clipLengthBeats);
    std::map<int, std::int64_t> sounding;                    // key -> start sample (clip-relative)
    std::map<int, int> velocities;
    std::size_t next = 0;
    std::array<float, Block> left{}, right{};
    for (std::int64_t pos = 0; pos < length; pos += Block) {
        const int frames = int(std::min<std::int64_t>(Block, length - pos));
        MidiBuffer midi;
        for (; next < events.size() && events[next].sample < pos + frames; ++next) {
            MidiEvent e = events[next].event;
            e.offset = int(std::max<std::int64_t>(0, events[next].sample - pos));
            midi.add(e);
        }
        midi.sort();
        float* channels[]{left.data(), right.data()};
        ProcessContext c;
        c.audio = AudioView{channels, 2, frames};
        c.midi = &midi;
        c.sampleRate = Rate;
        c.bpm = bpm;
        c.playing = true;
        c.timeSigNumerator = numerator;
        c.timeSigDenominator = denominator;
        c.beatsPerBar = numerator * 4.0 / denominator;
        c.samplePosition = origin + pos;
        c.beat = double(origin + pos) / samplesPerBeat;
        player.process(c);
        for (const auto& e : midi.events()) {
            const bool on = (e.status & 0xF0) == 0x90 && e.data2 > 0;
            const bool off = (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0);
            if (!on && !off) continue;
            const std::int64_t at = pos + e.offset;
            if (const auto it = sounding.find(e.data1); it != sounding.end()) {   // an on or off ends the one before
                MidiNote n;
                n.note = e.data1;
                n.startBeat = double(it->second) / samplesPerBeat;
                n.lengthBeats = std::max(1.0 / samplesPerBeat, double(at - it->second) / samplesPerBeat);
                n.velocity = velocities[e.data1];
                out.push_back(n);
                sounding.erase(it);
            }
            if (on) { sounding[e.data1] = at; velocities[e.data1] = e.data2; }
        }
    }
    for (const auto& [key, start] : sounding) {            // still sounding at the clip's end
        MidiNote n;
        n.note = key;
        n.startBeat = double(start) / samplesPerBeat;
        n.lengthBeats = std::max(1.0 / samplesPerBeat, double(length - start) / samplesPerBeat);
        n.velocity = velocities[key];
        out.push_back(n);
    }
    std::stable_sort(out.begin(), out.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.note < b.note;
    });
    int id = 1;
    for (auto& n : out) n.id = id++;
    return out;
}

} // namespace chords
