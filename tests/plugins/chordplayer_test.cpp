#include "testkit.h"
#include "plugins/chordplayer/chordplayer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

using namespace chords;

namespace {

struct Event { long sample; bool on; int key; int velocity; };

// Drives a ChordPlayer block by block like the engine does. Presses go into the block holding
// their sample. The transport may play (song beats from sample 0) or be stopped.
struct Rig {
    ChordPlayer player;
    double rate = 48000, bpm = 120;
    int numerator = 4, denominator = 4;
    int block = 256;
    long position = 0;                    // transport position (samples) while playing
    long clock = 0;                       // wall clock (samples), for events while stopped
    bool playing = true;
    std::vector<Event> out;
    std::map<long, std::vector<MidiEvent>> pending;   // wall-clock sample -> events

    Rig(int autoplay = 3)
    {
        player.prepare(rate);
        player.setAutoplay(autoplay);
        std::array<ChordPlayer::Strip, 8> strips{};
        const char* names[8]{"Am", "G", "F", "E", "C", "Dm", "G7", "Cmaj7"};
        for (int i = 0; i < 8; ++i) strips[std::size_t(i)] = ChordPlayer::stripOf(*parse(names[i]));
        player.setStrips(strips);
    }
    void at(long sample, std::uint8_t status, int key, int velocity) { pending[sample].push_back({0, status, std::uint8_t(key), std::uint8_t(velocity)}); }
    void press(long sample, int strip, bool bass = false, int velocity = 100)
    {
        at(sample, 0x90, (bass ? ChordPlayer::BassKeys : ChordPlayer::ChordKeys)[std::size_t(strip)], velocity);
    }
    void release(long sample, int strip, bool bass = false)
    {
        at(sample, 0x80, (bass ? ChordPlayer::BassKeys : ChordPlayer::ChordKeys)[std::size_t(strip)], 0);
    }
    // Runs until the wall clock reaches `until`.
    void run(long until)
    {
        float left[4096]{}, right[4096]{};
        while (clock < until) {
            const int frames = int(std::min<long>(block, until - clock));
            MidiBuffer midi;
            for (auto it = pending.lower_bound(clock); it != pending.end() && it->first < clock + frames; ++it)
                for (auto e : it->second) { e.offset = int(it->first - clock); midi.add(e); }
            midi.sort();
            float* channels[]{left, right};
            ProcessContext c;
            c.audio = AudioView{channels, 2, frames};
            c.midi = &midi;
            c.sampleRate = rate; c.bpm = bpm; c.playing = playing;
            c.timeSigNumerator = numerator; c.timeSigDenominator = denominator;
            c.beatsPerBar = numerator * 4.0 / denominator;
            c.samplePosition = position;
            c.beat = position * bpm / 60.0 / rate;
            player.process(c);
            for (const auto& e : midi.events()) {
                const bool on = (e.status & 0xF0) == 0x90 && e.data2 > 0;
                const bool off = (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0);
                if (on || off) out.push_back({clock + e.offset, on, e.data1, e.data2});
            }
            clock += frames;
            if (playing) position += frames;
        }
    }
    long beatSample(double beat) const { return std::lround(beat * 60.0 / bpm * rate); }
    std::vector<long> onsets(int fromKey = 0, int toKey = 127) const
    {
        std::set<long> s;
        for (const auto& e : out) if (e.on && e.key >= fromKey && e.key <= toKey) s.insert(e.sample);
        return {s.begin(), s.end()};
    }
    int sounding() const                  // notes on and not yet off
    {
        std::map<int, int> n;
        for (const auto& e : out) n[e.key] += e.on ? 1 : -1;
        int total = 0;
        for (const auto& [k, v] : n) total += v;
        return total;
    }
};

std::set<int> pitchClassesOn(const std::vector<Event>& out, long from, long to, int minKey = 53)
{
    std::set<int> s;
    for (const auto& e : out) if (e.on && e.sample >= from && e.sample < to && e.key >= minKey) s.insert(e.key % 12);
    return s;
}

} // namespace

TEST(chordplayer, patterns_fit_their_meter)
{
    Pattern p;
    for (int mode = 1; mode <= 4; ++mode)
        for (auto [n, d] : std::vector<std::pair<int, int>>{{4, 4}, {3, 4}, {6, 8}, {7, 8}, {2, 4}, {5, 4}, {12, 8}}) {
            for (Part part : {Part::Chord, Part::Bass}) {
                buildPattern(mode, n, d, part, p);
                CHECK(p.count > 0);
                CHECK_NEAR(p.barBeats, n * 4.0 / d, 1e-12);
                for (int i = 0; i < p.count; ++i) {
                    const auto& s = p.steps[std::size_t(i)];
                    CHECK(s.beat >= 0 && s.beat < p.barBeats && s.length > 0 && s.velocity >= 1 && s.velocity <= 127);
                    if (i > 0) CHECK(s.beat >= p.steps[std::size_t(i - 1)].beat);
                    if (part == Part::Bass) CHECK(s.voice == VoiceBass || s.voice == VoiceBassOctave);
                }
            }
        }
    buildPattern(0, 4, 4, Part::Chord, p);
    CHECK(p.count == 0);
    CHECK(autoplayName(0) == "Off" && autoplayName(3) == "Arpeggio" && autoplayName(9).empty());

    // 3/4 block is oom-pah-pah: bass alone on one, the chord on two and three.
    buildPattern(1, 3, 4, Part::Chord, p);
    std::vector<std::pair<double, int>> waltz;
    for (int i = 0; i < p.count; ++i) waltz.push_back({p.steps[std::size_t(i)].beat, p.steps[std::size_t(i)].voice});
    CHECK((waltz == std::vector<std::pair<double, int>>{{0.0, VoiceBass}, {1.0, VoiceChord}, {2.0, VoiceChord}}));
    // 6/8 block rolls: bass on the two dotted beats, the chord on the other eighths.
    buildPattern(1, 6, 8, Part::Chord, p);
    std::vector<double> bass, chord;
    for (int i = 0; i < p.count; ++i) (p.steps[std::size_t(i)].voice == VoiceBass ? bass : chord).push_back(p.steps[std::size_t(i)].beat);
    CHECK((bass == std::vector<double>{0.0, 1.5}));
    CHECK((chord == std::vector<double>{0.5, 1.0, 2.0, 2.5}));
    // 7/8 counts 2+2+3: the bass on each group.
    buildPattern(1, 7, 8, Part::Chord, p);
    bass.clear();
    for (int i = 0; i < p.count; ++i) if (p.steps[std::size_t(i)].voice == VoiceBass) bass.push_back(p.steps[std::size_t(i)].beat);
    CHECK((bass == std::vector<double>{0.0, 1.0, 2.0}));
}

TEST(chordplayer, arpeggio_notes_land_on_the_exact_sample_at_any_tempo_and_meter)
{
    struct Case { double bpm; int n, d; };
    for (const Case c : {Case{120, 4, 4}, Case{90, 4, 4}, Case{137.5, 3, 4}, Case{100, 6, 8}, Case{76, 7, 8}}) {
        Rig rig(3);                                                  // arpeggio in eighths
        rig.bpm = c.bpm; rig.numerator = c.n; rig.denominator = c.d;
        rig.block = 128;
        rig.press(0, 0);                                             // Am at the start of the song
        const double barBeats = c.n * 4.0 / c.d;
        rig.run(rig.beatSample(2 * barBeats));
        // The right hand rises in eighths, every one on its own sample.
        const auto rightHand = rig.onsets(53, 127);
        std::vector<long> expected;
        for (double b = 0; b < 2 * barBeats - 1e-9; b += 0.5) expected.push_back(rig.beatSample(b));
        CHECK(rightHand == expected);
        // The bass on the bar's strong beats only.
        for (long s : rig.onsets(0, 52)) {
            const double beat = s * c.bpm / 60.0 / rig.rate;
            const double inBar = std::fmod(beat + 1e-9, barBeats);
            CHECK(std::abs(inBar - std::round(inBar * 2) / 2) < c.bpm / 60.0 / rig.rate);   // within a sample
        }
        CHECK(pitchClassesOn(rig.out, 0, rig.beatSample(2 * barBeats)) == (std::set<int>{9, 0, 4}));   // A C E
    }
}

TEST(chordplayer, a_press_latches_another_strip_takes_over_and_the_same_strip_stops)
{
    Rig rig(3);
    rig.press(0, 0);                                                 // Am
    rig.release(rig.beatSample(0.2), 0);                             // let go: it keeps playing
    const long g = rig.beatSample(4.0) + 333;                        // G, a little after bar two
    rig.press(g, 1);
    rig.run(rig.beatSample(4.0));
    CHECK(rig.player.running() && rig.player.activeStrip() == 0);
    CHECK(rig.onsets(53).size() == 8);                               // a whole bar of eighths after the let-go
    rig.run(rig.beatSample(8.0));
    CHECK(rig.player.activeStrip() == 1);
    // At the press everything of Am ends, and G strikes at once, on that very sample.
    int offsAtG = 0, onsAtG = 0;
    for (const auto& e : rig.out) if (e.sample == g) (e.on ? onsAtG : offsAtG)++;
    CHECK(offsAtG >= 1 && onsAtG >= 4);                              // the chord and its bass
    CHECK(pitchClassesOn(rig.out, g, rig.beatSample(8.0)) == (std::set<int>{7, 11, 2}));   // G B D
    // The grid carries on: the next eighth after the press is on the song's grid.
    bool onGrid = false;
    for (const auto& e : rig.out) if (e.on && e.sample > g && e.sample == rig.beatSample(4.5)) onGrid = true;
    CHECK(onGrid);

    // G again: it stops, everything is released and the track may go idle.
    const long again = rig.beatSample(8.25);
    rig.press(again, 1);
    rig.run(rig.beatSample(10.0));
    CHECK(!rig.player.running() && rig.player.activeStrip() == -1);
    CHECK(rig.sounding() == 0);
    for (const auto& e : rig.out) if (e.on) CHECK(e.sample <= again);
}

TEST(chordplayer, autoplay_off_holds_the_chord_while_the_key_is_down)
{
    Rig rig(AutoplayOff);
    rig.press(1000, 4);                                              // C
    rig.run(20000);
    CHECK(rig.player.activeStrip() == 4 && !rig.player.running());   // held, not latched
    CHECK(pitchClassesOn(rig.out, 0, 20000) == (std::set<int>{0, 4, 7}));
    CHECK(rig.onsets(0, 127) == std::vector<long>{1000});            // one strike, no pattern
    rig.release(30000, 4);
    rig.run(40000);
    CHECK(rig.sounding() == 0 && rig.player.activeStrip() == -1);
    for (const auto& e : rig.out) if (!e.on) CHECK(e.sample == 30000);

    // The bass area plays the bass alone.
    Rig bass(AutoplayOff);
    bass.press(0, 0, true);
    bass.run(1000);
    CHECK(bass.out.size() == 1 && bass.out[0].key % 12 == 9 && bass.out[0].key <= 52);
    CHECK(bass.player.activeBass());
}

TEST(chordplayer, stopped_it_runs_on_its_own_clock_and_the_transport_ends_it)
{
    Rig rig(3);
    rig.playing = false;
    const long press = 5000;
    rig.press(press, 2);                                             // F, transport stopped
    rig.run(press + rig.beatSample(2.0));
    CHECK(rig.player.running());
    // Eighths from the press itself, at the project tempo.
    const auto rh = rig.onsets(53);
    CHECK(!rh.empty() && rh.front() == press);
    for (std::size_t i = 1; i < rh.size(); ++i) CHECK(std::abs((rh[i] - press) - rig.beatSample(0.5 * double(i))) <= 1);
    // The transport starts: what played stops, and the latch with it.
    rig.playing = true;
    rig.run(rig.clock + 4096);
    CHECK(!rig.player.running() && rig.sounding() == 0);

    // While playing, a jump (a seek or a loop) ends it too.
    Rig loop(3);
    loop.press(0, 0);
    loop.run(loop.beatSample(1.0));
    CHECK(loop.player.running());
    loop.position = loop.beatSample(16.0);                           // jump
    loop.run(loop.clock + 512);
    CHECK(!loop.player.running() && loop.sounding() == 0);

    // And an all-notes-off does.
    Rig cc(3);
    cc.press(0, 0);
    cc.at(cc.beatSample(1.0), 0xB0, 123, 0);
    cc.run(cc.beatSample(2.0));
    CHECK(!cc.player.running() && cc.sounding() == 0);
}

TEST(chordplayer, other_midi_passes_through_and_empty_strips_stay_silent)
{
    Rig rig(3);
    rig.at(100, 0x90, 61, 90);                                       // C#4: not a strip key
    rig.at(200, 0xB0, 64, 127);                                      // the sustain pedal
    rig.at(300, 0x80, 61, 0);
    rig.run(1000);
    CHECK(rig.out.size() == 2 && rig.out[0].key == 61 && rig.out[0].sample == 100 && rig.out[1].sample == 300);
    CHECK(!rig.player.running());

    std::array<ChordPlayer::Strip, 8> strips{};                      // all empty
    rig.player.setStrips(strips);
    rig.press(2000, 0);
    rig.run(10000);
    CHECK(rig.out.size() == 2 && !rig.player.running());

    bool bass = false;
    CHECK(ChordPlayer::stripOfKey(67, bass) == 4 && !bass);
    CHECK(ChordPlayer::stripOfKey(41, bass) == 3 && bass);
    CHECK(ChordPlayer::stripOfKey(61, bass) == -1);
}

TEST(chordplayer, chord_changes_are_voice_led_and_velocity_follows_the_press)
{
    Rig rig(1);                                                      // block chords
    rig.press(0, 0, false, 60);                                      // Am, softly
    rig.press(rig.beatSample(4.0), 1, false, 127);                   // G, hard
    rig.run(rig.beatSample(8.0));
    std::vector<int> am, g;
    int softest = 127, loudest = 0;
    for (const auto& e : rig.out) {
        if (!e.on || e.key < 53) continue;
        if (e.sample == 0) am.push_back(e.key);
        if (e.sample == rig.beatSample(4.0)) g.push_back(e.key);
        if (e.sample < rig.beatSample(4.0)) softest = std::min(softest, e.velocity);
        else loudest = std::max(loudest, e.velocity);
    }
    std::sort(am.begin(), am.end());
    std::sort(g.begin(), g.end());
    CHECK(am.size() == 3 && g.size() == 3);
    for (std::size_t i = 0; i < am.size() && i < g.size(); ++i) CHECK(std::abs(am[i] - g[i]) <= 2);   // each finger a step
    CHECK(softest < 70 && loudest > 100);
}
