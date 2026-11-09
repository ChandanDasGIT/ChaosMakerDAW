#include "testkit.h"
#include "plugins/chordplayer/chordplayer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
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
    std::vector<std::pair<long, int>> pedal;      // sustain pedal changes: sample, value
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
                if ((e.status & 0xF0) == 0xB0 && e.data1 == 64) pedal.push_back({clock + e.offset, e.data2});
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

namespace {

using Meter = std::pair<int, int>;
const std::vector<Meter> WrittenMeters{{4, 4}, {3, 4}, {6, 8}, {7, 8}, {5, 4}};

bool isLeftHand(Kind k) { return k == Kind::Bass || k == Kind::Low; }

} // namespace

TEST(chordplayer, patterns_fit_their_meter)
{
    Pattern p;
    const std::vector<Meter> meters{{4, 4}, {3, 4}, {6, 8}, {7, 8}, {5, 4}, {2, 4}, {12, 8}, {9, 8}, {5, 8},
                                    {6, 4}, {7, 4}, {11, 8}, {3, 8}, {7, 16}, {1, 4}, {2, 2}, {13, 8}};
    for (int mode = 1; mode <= 4; ++mode)
        for (auto [n, d] : meters)
            for (Part part : {Part::Chord, Part::Bass}) {
                buildPattern(mode, n, d, part, p);
                CHECK(p.count > 0);
                CHECK(p.bars == 2);
                CHECK_NEAR(p.barBeats, n * 4.0 / d, 1e-12);
                CHECK(p.pedal == (mode == 1 || mode == 4));
                bool hasDownbeat = false;
                for (int i = 0; i < p.count; ++i) {
                    const auto& s = p.steps[std::size_t(i)];
                    const double barEnd = (std::floor(s.beat / p.barBeats + 1e-9) + 1) * p.barBeats;
                    CHECK(s.beat >= 0 && s.beat < p.lengthBeats());
                    CHECK(s.length > 0 && s.beat + s.length <= barEnd + 1e-9);   // inside its bar
                    CHECK(s.velocity >= 1 && s.velocity <= 127);
                    CHECK(std::abs(s.nudgeMs) <= 5.0);
                    if (i > 0) CHECK(s.beat >= p.steps[std::size_t(i - 1)].beat);
                    if (part == Part::Bass) CHECK(isLeftHand(s.kind));
                    if (s.beat == 0.0) hasDownbeat = true;
                }
                CHECK(hasDownbeat);
            }
    buildPattern(0, 4, 4, Part::Chord, p);
    CHECK(p.count == 0 && !p.pedal);
    CHECK(autoplayName(0) == "Off" && autoplayName(1) == "Flowing" && autoplayName(2) == "Pop");
    CHECK(autoplayName(3) == "Comping" && autoplayName(4) == "Ballad" && autoplayName(9).empty());

    std::array<double, 32> g{};
    CHECK(groupStarts(4, 4, g) == 2 && g[1] == 2.0);
    CHECK(groupStarts(6, 8, g) == 2 && g[1] == 1.5);
    CHECK(groupStarts(7, 8, g) == 3 && g[1] == 1.0 && g[2] == 2.0);   // 2+2+3
    CHECK(groupStarts(5, 4, g) == 2 && g[1] == 3.0);                  // 3+2
    CHECK(groupStarts(5, 8, g) == 2 && g[1] == 1.5);                  // 3+2
    CHECK(groupStarts(3, 4, g) == 1);
}

TEST(chordplayer, bar_two_varies_bar_one_and_the_groups_carry_the_accents)
{
    Pattern p;
    for (int mode = 1; mode <= 4; ++mode)
        for (auto [n, d] : WrittenMeters) {
            buildPattern(mode, n, d, Part::Chord, p);
            std::array<std::vector<std::tuple<double, int, int, int>>, 2> bars;
            std::array<double, 32> groups{};
            const int groupCount = groupStarts(n, d, groups);
            for (int bar = 0; bar < 2; ++bar) {
                int downbeat = 0, elsewhere = 0, weakSum = 0, weakCount = 0;
                std::map<double, int> groupLoudest;
                for (int i = 0; i < p.count; ++i) {
                    const auto& s = p.steps[std::size_t(i)];
                    const double inBar = s.beat - bar * p.barBeats;
                    if (inBar < -1e-9 || inBar >= p.barBeats - 1e-9) continue;
                    bars[std::size_t(bar)].push_back({inBar, int(s.kind), s.degree, s.octave});
                    bool atGroup = false;
                    for (int k = 0; k < groupCount; ++k)
                        if (std::abs(groups[std::size_t(k)] - inBar) < 1e-9) atGroup = true;
                    if (inBar < 1e-9) downbeat = std::max(downbeat, s.velocity);
                    else elsewhere = std::max(elsewhere, s.velocity);
                    if (atGroup) groupLoudest[inBar] = std::max(groupLoudest[inBar], s.velocity);
                    else { weakSum += s.velocity; ++weakCount; }
                }
                CHECK(downbeat >= elsewhere);                              // the bar's one is the loudest
                for (const auto& [beat, loudest] : groupLoudest)           // every group start stands out
                    CHECK(weakCount == 0 || loudest * weakCount > weakSum);
            }
            CHECK(bars[0] != bars[1]);
        }
}

TEST(chordplayer, each_meter_has_its_own_rhythm_not_four_four_cut_short)
{
    // Heard at one volume, the meter has to come from the rhythm: Flowing and Pop in 3/4, 6/8,
    // 7/8 and 5/4 are not the 4/4 bar cut to length, and every group of the meter starts with a
    // left-hand note or the right hand starting low again (its root, in its lowest octave).
    const auto same = [](const PatternStep& a, const PatternStep& b) {
        return std::abs(a.beat - b.beat) < 1e-9 && a.kind == b.kind && a.degree == b.degree && a.octave == b.octave;
    };
    for (const int mode : {1, 2}) {
        Pattern four;
        buildPattern(mode, 4, 4, Part::Chord, four);
        for (const auto [n, d] : {std::pair{3, 4}, std::pair{6, 8}, std::pair{7, 8}, std::pair{5, 4}}) {
            Pattern p;
            buildPattern(mode, n, d, Part::Chord, p);
            const double cut = std::min(p.barBeats, 4.0);
            std::vector<PatternStep> mine, theirs;
            for (int i = 0; i < p.count; ++i) if (p.steps[std::size_t(i)].beat < cut) mine.push_back(p.steps[std::size_t(i)]);
            for (int i = 0; i < four.count; ++i) if (four.steps[std::size_t(i)].beat < cut) theirs.push_back(four.steps[std::size_t(i)]);
            bool identical = mine.size() == theirs.size();
            for (std::size_t i = 0; identical && i < mine.size(); ++i) identical = same(mine[i], theirs[i]);
            CHECK(!identical);

            std::array<double, 32> starts{};
            const int count = groupStarts(n, d, starts);
            for (int bar = 0; bar < p.bars; ++bar)
                for (int g = 0; g < count; ++g) {
                    const double at = bar * p.barBeats + starts[std::size_t(g)];
                    bool marked = false;
                    for (int i = 0; i < p.count; ++i) {
                        const auto& s = p.steps[std::size_t(i)];
                        if (std::abs(s.beat - at) > 1e-9) continue;
                        marked |= s.kind == Kind::Bass || s.kind == Kind::Low || s.kind == Kind::Chord
                                  || (s.kind == Kind::Tone && s.degree <= 2 && s.octave == 0);
                    }
                    CHECK(marked);
                }
        }
    }
}

TEST(chordplayer, arpeggios_use_the_whole_chord_across_the_bar)
{
    // The bug of the first version: the run restarted on every beat, so only two notes played.
    for (int mode : {1, 2})
        for (auto [n, d] : WrittenMeters) {
            Rig rig(mode);
            rig.numerator = n; rig.denominator = d;
            rig.press(0, 0);                                         // Am
            const double bar = n * 4.0 / d;
            rig.run(rig.beatSample(2 * bar));
            for (int b = 0; b < 2; ++b) {
                std::set<int> keys, rightHand;
                for (const auto& e : rig.out)
                    if (e.on && e.sample >= rig.beatSample(b * bar) && e.sample < rig.beatSample((b + 1) * bar)) {
                        keys.insert(e.key);
                        if (e.key >= 53) rightHand.insert(e.key);
                    }
                CHECK(keys.size() >= 5);
                CHECK(rightHand.size() >= 3);
            }
            CHECK(pitchClassesOn(rig.out, 0, rig.beatSample(2 * bar), 0) == (std::set<int>{9, 0, 4}));   // A C E
        }
}

TEST(chordplayer, pattern_notes_land_on_the_exact_sample_at_any_tempo_and_meter)
{
    struct Case { double bpm; int n, d; };
    for (int mode = 1; mode <= 4; ++mode)
        for (const Case c : {Case{120, 4, 4}, Case{90, 4, 4}, Case{137.5, 3, 4}, Case{100, 6, 8}, Case{76, 7, 8}, Case{110, 5, 4}}) {
            Rig rig(mode);
            rig.bpm = c.bpm; rig.numerator = c.n; rig.denominator = c.d;
            rig.block = 128;
            rig.press(0, 0);                                         // Am at the start of the song
            Pattern p;
            buildPattern(mode, c.n, c.d, Part::Chord, p);
            const double loop = p.lengthBeats();
            rig.run(rig.beatSample(2 * loop));
            // Where every step of two loops must sound: on its grid sample, or its few ms off.
            std::set<long> expected;
            const double perMs = c.bpm / 60000.0;
            for (int k = 0; k < 2; ++k)
                for (int i = 0; i < p.count; ++i) {
                    const auto& s = p.steps[std::size_t(i)];
                    const double at = k * loop + s.beat + s.nudgeMs * perMs;
                    const int notes = s.kind == Kind::Roll ? 3 : 1;
                    for (int r = 0; r < notes; ++r) expected.insert(rig.beatSample(at + r * s.spreadMs * perMs));
                }
            const auto onsets = rig.onsets();
            CHECK(!onsets.empty());
            for (long s : onsets) CHECK(expected.count(s) == 1);
            for (long s : expected) if (s < rig.beatSample(2 * loop)) CHECK(std::binary_search(onsets.begin(), onsets.end(), s));
        }
}

TEST(chordplayer, the_pedalled_patterns_change_the_pedal_on_every_chord)
{
    Rig rig(1);                                                      // Flowing
    rig.press(0, 0);
    rig.press(rig.beatSample(4.0), 1);
    const long stop = rig.beatSample(6.0);
    rig.press(stop, 1);                                              // G again: stop
    rig.run(rig.beatSample(8.0));
    const long lag = std::lround(0.030 * rig.rate);
    CHECK((rig.pedal == std::vector<std::pair<long, int>>{
        {lag, 127}, {rig.beatSample(4.0), 0}, {rig.beatSample(4.0) + lag, 127}, {stop, 0}}));
    CHECK(rig.sounding() == 0);

    Rig comping(3);                                                  // not pedalled
    comping.press(0, 0);
    comping.run(comping.beatSample(8.0));
    CHECK(comping.pedal.empty());
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
    CHECK(rig.onsets(53).size() >= 5);                               // the bar's chords go on after the let-go
    rig.run(rig.beatSample(8.0));
    CHECK(rig.player.activeStrip() == 1);
    // At the press everything of Am ends, and G strikes at once, on that very sample.
    int offsAtG = 0, onsAtG = 0;
    for (const auto& e : rig.out) if (e.sample == g) (e.on ? onsAtG : offsAtG)++;
    CHECK(offsAtG >= 1 && onsAtG >= 4);                              // the chord and its bass
    CHECK(pitchClassesOn(rig.out, g, rig.beatSample(8.0)) == (std::set<int>{7, 11, 2}));   // G B D
    // The grid carries on: the next chord after the press is on the song's grid (bar two's 2&).
    bool onGrid = false;
    for (const auto& e : rig.out) if (e.on && e.sample > g && e.sample == rig.beatSample(5.5)) onGrid = true;
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
    Rig rig(1);                                                      // Flowing: a note on every eighth
    rig.playing = false;
    const long press = 5000;
    rig.press(press, 2);                                             // F, transport stopped
    rig.run(press + rig.beatSample(2.0));
    CHECK(rig.player.running());
    // Eighths from the press itself, at the project tempo.
    const auto rh = rig.onsets();
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
    Rig rig(3);                                                      // comping: chords on both bars' one
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

namespace {

// The right hand a held press strikes (every note but the lowest, the bass), sorted.
std::vector<int> struckHand(int key, std::vector<int> before = {})
{
    Rig rig(AutoplayOff);
    long t = 0;
    for (int k : before) { rig.at(t, 0x90, k, 100); rig.at(t + 10, 0x80, k, 0); t += 20; }
    rig.at(t + 100, 0x90, key, 100);
    rig.run(t + 2000);
    std::vector<int> keys;
    for (const auto& e : rig.out) if (e.on && e.sample == t + 100) keys.push_back(e.key);
    std::sort(keys.begin(), keys.end());
    if (!keys.empty()) keys.erase(keys.begin());                 // the bass
    return keys;
}

double mean(const std::vector<int>& v)
{
    double s = 0;
    for (int x : v) s += x;
    return v.empty() ? 0 : s / double(v.size());
}

} // namespace

TEST(chordplayer, the_three_zones_voice_the_chord_low_middle_and_high)
{
    using P = ChordPlayer;
    constexpr int Roots[8]{9, 7, 5, 4, 0, 2, 7, 0};                  // Am G F E C Dm G7 Cmaj7
    for (int strip = 0; strip < 8; ++strip) {
        const int root = Roots[strip];
        const auto low = struckHand(P::LowChordKeys[std::size_t(strip)]);
        const auto middle = struckHand(P::ChordKeys[std::size_t(strip)]);
        const auto high = struckHand(P::HighChordKeys[std::size_t(strip)]);
        CHECK(low.size() >= 3 && middle.size() == low.size() && high.size() == low.size());
        // An octave apart, from the root: two octaves up from its octave 3, 4 or 5.
        for (int z = 0; z < 3; ++z) CHECK(zoneRange(z, root).low == 48 + 12 * z + root && zoneRange(z, root).high == zoneRange(z, root).low + 24);
        for (int k : low) CHECK(k >= zoneRange(0, root).low && k <= zoneRange(0, root).high);
        for (int k : middle) CHECK(k >= zoneRange(1, root).low && k <= zoneRange(1, root).high);
        for (int k : high) CHECK(k >= zoneRange(2, root).low && k <= zoneRange(2, root).high);
        CHECK(mean(low) < mean(middle) && mean(middle) < mean(high));
        std::set<int> a, b, c;                                   // the same chord in all three
        for (int k : low) a.insert(k % 12);
        for (int k : middle) b.insert(k % 12);
        for (int k : high) c.insert(k % 12);
        CHECK(a == b && b == c);
        // The zone keys name the same zones: the same notes as the octaves.
        CHECK(struckHand(P::ChordKeys[std::size_t(strip)], {P::ZoneKeys[0]}) == low);
        CHECK(struckHand(P::ChordKeys[std::size_t(strip)], {P::ZoneKeys[2]}) == high);
        CHECK(struckHand(P::ChordKeys[std::size_t(strip)], {P::ZoneKeys[2], P::ZoneKeys[1]}) == middle);
    }
    // What the strips send for each mapping.
    const auto octave = P::triggerNotes(3, false, P::High, P::KeyMapping::Octaves);
    CHECK(octave.count == 1 && octave.keys[0] == 89 && octave.stripKey == 89);
    const auto zoned = P::triggerNotes(3, false, P::Low, P::KeyMapping::ZoneKeys);
    CHECK(zoned.count == 2 && zoned.keys[0] == 42 && zoned.keys[1] == 65 && zoned.stripKey == 65);
    const auto bass = P::triggerNotes(3, true, P::High, P::KeyMapping::ZoneKeys);
    CHECK(bass.count == 1 && bass.keys[0] == 41);
    bool isBass = true;
    int zone = 0;
    CHECK(P::stripOfKey(17, isBass, zone) == 3 && !isBass && zone == P::Low);
    CHECK(P::stripOfKey(96, isBass, zone) == 7 && zone == P::High);
    CHECK(P::stripOfKey(60, isBass, zone) == 0 && zone == -1);
    CHECK(P::zoneOfKey(44) == P::Middle && P::zoneOfKey(43) == -1);
}

TEST(chordplayer, zones_while_a_pattern_plays_and_the_zone_keys_reset_on_a_jump)
{
    // Another zone of the playing strip re-voices it (it does not stop), and its arpeggio moves too.
    Rig rig(1);                                                      // Flowing
    rig.press(0, 0);
    rig.at(rig.beatSample(4.0), 0x90, ChordPlayer::HighChordKeys[0], 100);
    rig.run(rig.beatSample(8.0));
    CHECK(rig.player.running() && rig.player.activeStrip() == 0 && rig.player.activeZone() == ChordPlayer::High);
    int topMiddle = 0, topHigh = 0;
    for (const auto& e : rig.out) {
        if (!e.on) continue;
        int& top = e.sample < rig.beatSample(4.0) ? topMiddle : topHigh;
        top = std::max(top, e.key);
    }
    CHECK(topHigh >= topMiddle + 10);
    // The zone key is not sounded or passed on.
    Rig keys(3);
    keys.at(0, 0x90, ChordPlayer::ZoneKeys[2], 100);
    keys.at(10, 0x80, ChordPlayer::ZoneKeys[2], 0);
    keys.run(1000);
    CHECK(keys.out.empty());
    // A jump puts the zone keys back to middle.
    keys.position = keys.beatSample(16.0);
    keys.run(keys.clock + 512);
    keys.press(keys.clock + 100, 0);
    keys.run(keys.clock + 4000);
    CHECK(keys.player.activeZone() == ChordPlayer::Middle);
    for (const auto& e : keys.out) if (e.on) CHECK(e.key <= zoneRange(ChordPlayer::Middle, 9).high);
}

TEST(chordplayer, a_chord_change_is_seamless_the_pattern_goes_on_and_the_new_chord_comes_in_on_its_next_note)
{
    constexpr double Guard = 0.12;                                   // the player's EntryGuard
    for (bool playing : {true, false})
        for (int mode = 1; mode <= 4; ++mode)
            for (double at : {0.0, 0.05, 1.3, 2.17, 2.6, 3.05, 3.8}) {
                Rig rig(mode);
                rig.playing = playing;
                rig.press(0, 0);                                     // Am, bar one
                const double b = 4.0 + at;
                const long change = rig.beatSample(b);
                rig.press(change, 1);                                // G, somewhere in bar two
                rig.run(rig.beatSample(12.0));
                // Where the pattern's next note is: the grid goes on (no restart), and a press a
                // hair late for a note plays that note at once.
                Pattern p;
                buildPattern(mode, 4, 4, Part::Chord, p);
                const double perMs = rig.bpm / 60000.0;
                double late = 1e9, lateNudge = 0, next = 1e9;
                for (int k = 0; k < 3; ++k)
                    for (int i = 0; i < p.count; ++i) {
                        const auto& st = p.steps[std::size_t(i)];
                        const double grid = k * p.lengthBeats() + st.beat;
                        if (grid > b - Guard && grid < b - 1e-9 && grid < late) { late = grid; lateNudge = st.nudgeMs; }
                        const double t = grid + st.nudgeMs * perMs;
                        if (t >= b - 1e-9) next = std::min(next, t);
                    }
                // A late press replays that note at the press (plus its own few ms late, never early).
                const long expected = late < 1e9 ? rig.beatSample(b + std::max(lateNudge, 0.0) * perMs) : rig.beatSample(next);
                long first = -1;
                for (const auto& e : rig.out) if (e.on && e.sample >= change) { first = e.sample; break; }
                CHECK(first == expected);                            // nothing struck before it
                // From then on only G's notes; the old ones are not cut at the click (they ring on).
                for (const auto& e : rig.out) {
                    if (e.on && e.sample >= change) CHECK(e.key % 12 == 7 || e.key % 12 == 11 || e.key % 12 == 2);
                    if (!e.on && expected != change) CHECK(e.sample != change);
                }
                if (mode == 1 || mode == 4)                          // the pedal is changed on that note too
                    CHECK(std::find(rig.pedal.begin(), rig.pedal.end(), std::pair<long, int>{expected, 0}) != rig.pedal.end());
            }
}

TEST(chordplayer, a_change_mid_bar_brings_the_new_root_in_with_its_first_note)
{
    // Am, then G halfway through bar two (and at other points): the first note of G has G2 under
    // it, in every pattern and meter, in the chord area and the bass area; nothing of Am after.
    // With bass with chords off, no bass comes in.
    for (const auto [n, d] : {std::pair{4, 4}, std::pair{3, 4}, std::pair{6, 8}, std::pair{7, 8}, std::pair{5, 4}})
        for (int mode = 1; mode <= 4; ++mode)
            for (const double at : {0.5, 0.75}) {                      // of a bar
                for (const bool bass : {false, true}) {
                    Rig rig(mode);
                    rig.numerator = n; rig.denominator = d;
                    const double bar = n * 4.0 / d;
                    rig.press(0, 0, bass);                                     // Am
                    const long change = rig.beatSample(bar + std::round(bar * at * 2) / 2);
                    rig.press(change, 1, bass);                                // G
                    rig.run(rig.beatSample(4 * bar));
                    long first = -1;
                    for (const auto& e : rig.out) if (e.on && e.sample >= change) { first = e.sample; break; }
                    CHECK(first >= change);
                    bool root = false;
                    for (const auto& e : rig.out)
                        if (e.on && e.sample >= change) {
                            CHECK(e.key % 12 == 7 || e.key % 12 == 11 || e.key % 12 == 2);
                            if (e.sample == first && e.key == 43) root = true;     // G2
                        }
                    CHECK(root);
                }
                Rig off(mode);
                off.numerator = n; off.denominator = d;
                off.player.setBassWithChords(false);
                const double bar = n * 4.0 / d;
                off.press(0, 0);
                const long change = off.beatSample(bar + std::round(bar * at * 2) / 2);
                off.press(change, 1);
                off.run(off.beatSample(4 * bar));
                for (const auto& e : off.out) if (e.on && e.sample >= change) CHECK(e.key >= 48);
            }
}

TEST(chordplayer, dynamics_same_volume_or_the_top_note_louder)
{
    for (int mode = 1; mode <= 4; ++mode) {
        Rig even(mode);
        even.press(0, 0);
        even.run(even.beatSample(8.0));
        std::set<int> velocities;
        for (const auto& e : even.out) if (e.on) velocities.insert(e.velocity);
        CHECK(velocities == std::set<int>{88});                      // every note, the bass too

        Rig top(mode);
        top.player.setDynamics(DynamicsTopNote);
        top.player.setAutoplay(AutoplayOff);                         // the held chord tells the top note
        top.press(0, 0);
        top.run(2000);
        int topKey = 0;
        for (const auto& e : top.out) if (e.on) topKey = std::max(topKey, e.key);
        for (const auto& e : top.out) if (e.on) CHECK(e.velocity == (e.key == topKey ? 104 : 80));
        top.release(3000, 0);
        top.run(4000);
        top.out.clear();
        top.player.setAutoplay(mode);
        top.press(5000, 0);
        top.run(5000 + top.beatSample(8.0));
        bool loud = false;
        for (const auto& e : top.out)
            if (e.on) {
                const bool sings = e.key >= 53 && e.key % 12 == topKey % 12;
                CHECK(e.velocity == (sings ? 104 : 80));
                loud = loud || sings;
            }
        CHECK(loud);                                                 // the top note is heard in every pattern
    }
}

TEST(chordplayer, meter_accents_follow_the_time_signature)
{
    // Every step's accent by where it falls: the one, the group starts, the beats (eighths in
    // x/8), the off-beat eighths, the sixteenths.
    const auto expect = [](double beatInBar, int n, int d) {
        std::array<double, 32> starts{};
        const int count = groupStarts(n, d, starts);
        const auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
        const auto on = [&](double unit) { return near(std::remainder(beatInBar, unit), 0); };
        if (near(beatInBar, 0)) return AccentOne;
        for (int g = 0; g < count; ++g) if (near(starts[std::size_t(g)], beatInBar)) return AccentGroup;
        if (d >= 8) return on(0.5) ? AccentPulse : AccentSixteenth;
        return on(1.0) ? AccentBeat : on(0.5) ? AccentOffbeat : AccentSixteenth;
    };
    for (int mode = 1; mode <= 4; ++mode)
        for (const auto [n, d] : {std::pair{4, 4}, std::pair{3, 4}, std::pair{6, 8}, std::pair{7, 8}, std::pair{5, 4}}) {
            Pattern p;
            buildPattern(mode, n, d, Part::Chord, p);
            for (int i = 0; i < p.count; ++i) {
                const auto& s = p.steps[std::size_t(i)];
                CHECK(s.accent == expect(std::fmod(s.beat, p.barBeats), n, d));
            }
        }
    Pattern pop;
    buildPattern(2, 4, 4, Part::Chord, pop);                         // 4/4: 1 loud, 3 a group, 16ths soft
    std::map<double, int> at;
    for (int i = 0; i < pop.count; ++i) at[pop.steps[std::size_t(i)].beat] = pop.steps[std::size_t(i)].accent;
    CHECK(at[0.0] == AccentOne && at[2.0] == AccentGroup && at[1.0] == AccentBeat && at[0.5] == AccentOffbeat && at[0.25] == AccentSixteenth);

    // The player plays them, scaled by the press; the other modes are untouched.
    Rig rig(2);
    rig.player.setDynamics(DynamicsMeter);
    rig.press(0, 0);
    rig.run(rig.beatSample(8.0));
    std::set<int> heard;
    for (const auto& e : rig.out) if (e.on) heard.insert(e.velocity);
    CHECK(heard.count(AccentOne) && heard.count(AccentGroup) && heard.count(AccentSixteenth));
    for (const int v : heard) CHECK(v == AccentOne || v == AccentGroup || v == AccentBeat || v == AccentOffbeat || v == AccentSixteenth);
    int first = 0;
    for (const auto& e : rig.out) if (e.on && e.sample == rig.out.front().sample) first = std::max(first, e.velocity);
    CHECK(first == AccentOne);
    Rig soft(2);
    soft.player.setDynamics(DynamicsMeter);
    soft.press(0, 0, false, 50);
    soft.run(soft.beatSample(4.0));
    for (const auto& e : soft.out) if (e.on && e.sample == soft.out.front().sample) CHECK(e.velocity == AccentOne / 2);
}

TEST(chordplayer, bass_with_chords_off_plays_the_right_hand_only)
{
    for (int mode = 0; mode <= 4; ++mode) {
        Rig rig(mode);
        rig.player.setBassWithChords(false);
        rig.press(0, 0);                                             // Am
        rig.run(rig.beatSample(8.0));
        CHECK(!rig.out.empty());
        for (const auto& e : rig.out) if (e.on) CHECK(e.key >= RightHand.low);
        if (mode != AutoplayOff) CHECK(rig.onsets().front() == 0);  // it starts at the press all the same

        Rig on(mode);                                                // on (the default): the bass is there
        on.press(0, 0);
        on.run(on.beatSample(8.0));
        bool low = false;
        for (const auto& e : on.out) if (e.on && e.key <= BassRange.high) low = true;
        CHECK(low);
    }
    Rig bass(1);                                                     // the bass area still plays the bass
    bass.player.setBassWithChords(false);
    bass.press(0, 0, true);
    bass.run(bass.beatSample(4.0));
    CHECK(!bass.out.empty());
    for (const auto& e : bass.out) if (e.on) CHECK(e.key <= BassRange.high + 12);
}

TEST(chordplayer, the_bass_part_is_root_third_fifth_octave)
{
    // Every pattern, both areas, all eight strips (slash-free here) and all zones: the bass note
    // in octave 2, the left hand only on its root, third, fifth and octave (C2 E2 G2 C3 for C),
    // all four of them in every pattern; the right hand above, in its zone.
    const std::array<const char*, 8> names{"Am", "G", "F", "E", "C", "Dm", "G7", "Cmaj7"};   // the rig's strips
    for (int mode = 0; mode <= 4; ++mode)
        for (int strip = 0; strip < 8; ++strip)
            for (int zone = 0; zone < 3; ++zone) {
                const auto chord = parse(names[std::size_t(strip)]);
                const int root = 36 + chord->root;
                const int third = root + (chord->intervals[1] % 12);
                const int fifth = root + (chord->intervals[2] % 12);
                Rig rig(mode);
                const auto keys = zone == 0 ? ChordPlayer::LowChordKeys : zone == 2 ? ChordPlayer::HighChordKeys : ChordPlayer::ChordKeys;
                rig.at(0, 0x90, keys[std::size_t(strip)], 100);
                rig.at(rig.beatSample(8.0), 0x80, keys[std::size_t(strip)], 0);
                rig.run(rig.beatSample(8.0));
                Rig bass(mode);
                bass.press(0, strip, true);
                bass.run(bass.beatSample(8.0));
                int lowest = 127;
                for (const auto& e : rig.out) if (e.on) lowest = std::min(lowest, e.key);
                CHECK(lowest == root);                                       // the bass note, octave 2
                for (const auto& e : rig.out)
                    if (e.on) CHECK(e.key <= root + 12 || e.key >= 48 + 12 * zone + chord->root);
                std::set<int> heard;
                for (const auto& e : bass.out)
                    if (e.on) {
                        CHECK(e.key == root || e.key == third || e.key == fifth || e.key == root + 12);
                        heard.insert(e.key);
                    }
                if (mode != AutoplayOff) CHECK(heard.size() == 4);
                CHECK(!bass.out.empty());
            }
    // A slash chord: its bass, in octave 2.
    Rig slash(AutoplayOff);
    std::array<ChordPlayer::Strip, 8> strips{};
    strips[0] = ChordPlayer::stripOf(*parse("G/B"));
    slash.player.setStrips(strips);
    slash.press(0, 0);
    slash.run(1000);
    int lowest = 127;
    for (const auto& e : slash.out) if (e.on) lowest = std::min(lowest, e.key);
    CHECK(lowest == 47);                                             // B2
}
