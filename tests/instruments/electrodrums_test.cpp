#include "testkit.h"
#include "instruments/electrodrums/electrodrums.h"
#include "instruments/electrodrums/electromap.h"
#include "instruments/electrodrums/electrosettings.h"
#include "instruments/electrodrums/electrovoices.h"
#include "instruments/drums/drummap.h"
#include "instruments/instrumentfactory.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace electrodrums;

namespace {

constexpr double rate = 48000.0;
constexpr Style styles[] = {Style::Style808, Style::Style909, Style::Studio};

// One whole hit, rendered in blocks of 256 until the voice ends (at most `maxSeconds`).
std::vector<float> hit(Drum drum, Style style, const Knobs& knobs = {}, float gain = 1.0f,
                       std::uint32_t seed = 7, double maxSeconds = 20.0)
{
    Voice voice;
    voice.start(drum, style, knobs, gain, rate, seed);
    std::vector<float> out;
    while (voice.active() && out.size() < std::size_t(maxSeconds * rate)) {
        const std::size_t at = out.size();
        out.resize(at + 256, 0.0f);
        voice.render(out.data() + at, 256);
    }
    return out;
}

float peak(const std::vector<float>& s, std::size_t from = 0, std::size_t to = ~std::size_t(0))
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, s.size()); ++i) p = std::max(p, std::abs(s[i]));
    return p;
}

// Frequency from upward zero crossings between two times.
double frequencyBetween(const std::vector<float>& s, double fromSeconds, double toSeconds)
{
    const auto a = std::size_t(fromSeconds * rate), b = std::size_t(toSeconds * rate);
    int crossings = 0;
    for (std::size_t i = a + 1; i < b; ++i)
        if (s[i - 1] < 0.0f && s[i] >= 0.0f) ++crossings;
    return crossings / (toSeconds - fromSeconds);
}

} // namespace

TEST(electrodrums, every_drum_in_every_style_sounds_stays_clean_and_ends)
{
    for (const Style style : styles) {
        for (int d = 0; d < DrumCount; ++d) {
            const auto drum = Drum(d);
            const auto out = hit(drum, style);
            const double length = recipe(drum, style, {}).lengthSeconds();
            CHECK(!out.empty());
            CHECK(out.size() <= std::size_t(length * rate) + 256);   // ends with its decay
            CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
            CHECK(peak(out) > 0.05f);                                 // audible
            CHECK(peak(out) < 1.0f);                                  // no clipping at full velocity
            // The end is already silent: no click when the voice stops.
            CHECK(peak(out, out.size() - std::size_t(0.01 * rate)) < 0.002f);
        }
    }
}

TEST(electrodrums, the_same_seed_gives_the_same_hit)
{
    for (const Style style : styles) {
        CHECK(hit(Drum::Snare, style, {}, 1.0f, 11) == hit(Drum::Snare, style, {}, 1.0f, 11));
        CHECK(hit(Drum::HatOpen, style, {}, 1.0f, 11) != hit(Drum::HatOpen, style, {}, 1.0f, 12));
    }
}

TEST(electrodrums, tune_moves_the_kick_pitch_and_decay_stretches_it)
{
    const auto plain = hit(Drum::Kick, Style::Style808);
    const auto up = hit(Drum::Kick, Style::Style808, {12.0, 1.0});
    const double f0 = frequencyBetween(plain, 0.2, 0.5), f12 = frequencyBetween(up, 0.2, 0.5);
    CHECK(std::abs(f0 - 49.0) < 3.0);                                  // the 808 kick's body
    CHECK(std::abs(f12 / f0 - 2.0) < 0.1);                             // an octave up
    // Out of range knobs are clamped: +40 semitones plays as +12.
    CHECK(hit(Drum::Kick, Style::Style808, {40.0, 1.0}) == up);

    const auto longer = hit(Drum::Kick, Style::Style808, {0.0, 2.0});
    CHECK(longer.size() > plain.size() * 19 / 10);
    CHECK(peak(longer, std::size_t(0.8 * rate)) > 3.0f * peak(plain, std::size_t(0.8 * rate)));
}

TEST(electrodrums, the_styles_differ_where_they_should)
{
    // 808: a long, deep kick; 909: shorter and higher, with a click on top.
    const auto k808 = hit(Drum::Kick, Style::Style808), k909 = hit(Drum::Kick, Style::Style909);
    CHECK(k808.size() > 2 * k909.size());
    CHECK(frequencyBetween(k909, 0.1, 0.3) > frequencyBetween(k808, 0.1, 0.3));
    // Studio: a tighter kick than the 808, with more of its energy in the mids (shell, beater).
    const auto kStudio = hit(Drum::Kick, Style::Studio);
    CHECK(kStudio.size() < k808.size() / 2);
    const auto midShare = [](const std::vector<float>& s) {   // above 100 Hz, first 100 ms
        std::vector<double> x(s.begin(), s.begin() + std::size_t(0.1 * rate));
        double total = 0.0, mid = 0.0;
        // Energy above 100 Hz (shell, beater): the difference from a one-pole low-pass there.
        const double a = std::exp(-2.0 * 3.14159265358979 * 100.0 / rate);
        double lp = 0.0;
        for (double v : x) { lp = (1.0 - a) * v + a * lp; total += v * v; mid += (v - lp) * (v - lp); }
        return mid / total;
    };
    CHECK(midShare(kStudio) > 1.25 * midShare(k808));
    // Studio toms carry an overtone: not a plain sine like the 808's.
    CHECK(recipe(Drum::TomMid, Style::Studio, {}).tones[1].frequency > 0.0);
    CHECK(recipe(Drum::TomMid, Style::Style808, {}).tones[1].frequency == 0.0);
    CHECK(styleId(Style::Studio) == "studio");
    // A clap comes in bursts: the level drops between the first bursts and comes back.
    const auto clap = hit(Drum::Clap, Style::Style808);
    const auto at = [](double ms) { return std::size_t(ms * rate / 1000.0); };
    CHECK(peak(clap, at(8), at(10.5)) < 0.5f * peak(clap, at(11), at(14)));
}

TEST(electrodrums, choke_and_gain)
{
    Voice open;
    open.start(Drum::HatOpen, Style::Style808, {}, 1.0f, rate, 3);
    std::vector<float> buffer(4800, 0.0f);
    open.render(buffer.data(), 2400);
    CHECK(open.active());
    open.choke();                                                      // a closed hat cuts it
    CHECK(open.choking());
    open.render(buffer.data() + 2400, 2400);
    CHECK(!open.active());
    CHECK(peak(buffer, 2400 + std::size_t(Voice::ChokeSeconds * rate) + 1) == 0.0f);

    // Gain scales the whole hit (drive is applied before it).
    const auto full = hit(Drum::Snare, Style::Style909, {}, 1.0f);
    const auto half = hit(Drum::Snare, Style::Style909, {}, 0.5f);
    CHECK(std::abs(peak(half) - 0.5f * peak(full)) < 1e-4f);
}

namespace {

// Plays `events` (key, velocity, frame) through the instrument for `frames` samples, stereo.
struct Hit { int key, velocity, at; std::uint8_t status = 0x90; };
std::vector<float> play(ElectroDrums& kit, std::vector<Hit> events, int frames)
{
    kit.prepare(rate, 512);
    std::vector<float> left(std::size_t(frames), 0.0f), right(std::size_t(frames), 0.0f);
    int done = 0;
    while (done < frames) {
        const int n = std::min(512, frames - done);
        MidiBuffer midi;
        for (const auto& e : events)
            if (e.at >= done && e.at < done + n)
                midi.add({e.at - done, e.status, std::uint8_t(e.key), std::uint8_t(e.velocity)});
        float* ch[2] = {left.data() + done, right.data() + done};
        ProcessContext context;
        context.audio = {ch, 2, n};
        context.midi = &midi;
        context.sampleRate = rate;
        kit.process(context);
        done += n;
    }
    CHECK(left == right);                                              // mono kit, both sides
    return left;
}

} // namespace

TEST(electrodrums, the_map_keeps_the_acoustic_keys_and_adds_clap_crash_ride)
{
    // Every acoustic kit key plays something here, so a pattern plays on either kit.
    for (const auto& pad : drummap::pads()) CHECK(electromap::find(pad.key) != nullptr);
    for (const auto& pad : drummap::pads()) {
        const auto name = electromap::nameOf(pad.key);
        if (pad.key != 64) CHECK(name == pad.name);                    // same names, but the edge key
    }
    CHECK(electromap::find(60)->drum == Drum::Kick);
    CHECK(electromap::find(63)->drum == Drum::Clap);
    CHECK(electromap::find(73)->drum == Drum::Crash);
    CHECK(electromap::find(75)->drum == Drum::Ride);
    CHECK(electromap::find(64)->drum == Drum::Snare);
    CHECK(electromap::find(59) == nullptr && electromap::nameOf(72).empty());
    for (int d = 0; d < DrumCount; ++d) {                              // every drum has a key
        bool found = false;
        for (const auto& pad : electromap::pads()) found = found || pad.drum == Drum(d);
        CHECK(found);
    }
}

TEST(electrodrums, the_factory_makes_the_kit_for_electronic_drums_tracks)
{
    const auto processor = createInstrument("Electronic Drums");
    CHECK(processor && processor->typeId() == "electrodrums");
}

TEST(electrodrums, every_key_sounds_others_stay_silent_and_velocity_sets_the_level)
{
    for (const auto& pad : electromap::pads()) {
        ElectroDrums kit;
        CHECK(peak(play(kit, {{pad.key, 127, 0}}, 4800)) > 0.05f);
    }
    ElectroDrums kit;
    CHECK(peak(play(kit, {{59, 127, 0}, {72, 127, 0}, {100, 127, 0}}, 4800)) == 0.0f);
    CHECK(kit.activeVoices() == 0);

    ElectroDrums loud, soft;
    const float pLoud = peak(play(loud, {{62, 127, 0}}, 9600)), pSoft = peak(play(soft, {{62, 40, 0}}, 9600));
    CHECK(std::abs(pSoft / pLoud - ElectroDrums::velocityGain(40)) < 0.01f);
    CHECK(ElectroDrums::velocityGain(127) == 1.0f && ElectroDrums::velocityGain(0) == 0.0f);
}

TEST(electrodrums, hits_are_one_shots_retrigger_cleanly_and_closed_hats_cut_open_ones)
{
    // A note-off right after the hit does not shorten it.
    ElectroDrums a, b;
    const auto held = play(a, {{60, 110, 0}}, 24000);
    const auto cut = play(b, {{60, 110, 0}, {60, 0, 10, 0x80}}, 24000);
    CHECK(held == cut);

    // The open hat rings; a closed hat 0.1 s later cuts it: one voice left, the closed one.
    ElectroDrums open, choked;
    play(open, {{70, 110, 0}}, 6000);
    CHECK(open.activeVoices() == 1);
    play(choked, {{70, 110, 0}, {66, 110, 4800}}, 6000);
    CHECK(choked.activeVoices() == 1);
    // The pedal cuts it too; a kick does not.
    ElectroDrums pedal, kick;
    play(pedal, {{70, 110, 0}, {68, 110, 2400}}, 3000);
    CHECK(pedal.activeVoices() == 1);
    play(kick, {{70, 110, 0}, {60, 110, 2400}}, 3000);
    CHECK(kick.activeVoices() == 2);

    // The same drum again cuts its last hit: never two snares at once.
    ElectroDrums snares;
    play(snares, {{62, 110, 0}, {62, 110, 1200}, {62, 110, 2400}}, 3000);
    CHECK(snares.activeVoices() == 1);

    // All sound off (CC 120) cuts everything within the choke time.
    ElectroDrums off;
    play(off, {{73, 110, 0}, {75, 110, 0}, {60, 110, 0}, {120, 0, 1000, 0xB0}}, 2000);
    CHECK(off.activeVoices() == 0);
}

TEST(electrodrums, the_voice_pool_never_runs_out)
{
    // 64 different-drum hits in one block (crash and ride ring for seconds): the pool is
    // reused, the newest hits sound, nothing is allocated or lost.
    ElectroDrums kit;
    std::vector<Hit> hits;
    const int keys[] = {73, 75, 60, 62, 63, 65, 67, 69, 61, 70};
    for (int i = 0; i < 64; ++i) hits.push_back({keys[i % 10], 100, i * 4});
    const auto out = play(kit, hits, 4800);
    CHECK(kit.activeVoices() <= ElectroDrums::Polyphony);
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
    CHECK(peak(out, 4000) > 0.01f);
}

TEST(electrodrums, settings_stay_in_range_and_ids_round_trip)
{
    Settings s;
    s.drum(Drum::Kick) = {40.0, 0.01, -99.0};
    s.drum(Drum::Snare) = {std::nan(""), std::numeric_limits<double>::infinity(), 3.0};
    s.volumeDb = 50.0;
    const auto c = sanitize(s);
    CHECK(c.drum(Drum::Kick).tuneSemitones == MaxTune && c.drum(Drum::Kick).decay == MinDecay
          && c.drum(Drum::Kick).levelDb == MinLevelDb);
    CHECK(c.drum(Drum::Snare).tuneSemitones == 0.0 && c.drum(Drum::Snare).decay == 1.0
          && c.drum(Drum::Snare).levelDb == 3.0);
    CHECK(c.volumeDb == MaxLevelDb);
    CHECK(sanitize(Settings{}) == Settings{} && !(c == Settings{}));

    for (const Style style : styles) {
        Style back = Style::Studio == style ? Style::Style808 : Style::Studio;
        CHECK(styleFromId(styleId(style), back) && back == style);
    }
    for (int d = 0; d < DrumCount; ++d) {
        Drum back = Drum::Count;
        CHECK(drumFromId(drumId(Drum(d)), back) && back == Drum(d));
        CHECK(!drumTitle(Drum(d)).empty());
    }
    Style style = Style::Studio;
    Drum drum = Drum::Ride;
    CHECK(!styleFromId("606", style) && style == Style::Studio);
    CHECK(!drumFromId("cowbell", drum) && drum == Drum::Ride);
}

TEST(electrodrums, the_kit_plays_its_settings)
{
    // Settings go in and come back (the parameters hold them).
    Settings s;
    s.style = Style::Style909;
    s.drum(Drum::Clap) = {-3.0, 2.0, -6.0};
    s.volumeDb = 4.5;
    ElectroDrums kit;
    kit.setSettings(s);
    CHECK(kit.settings() == s);

    // A drum's level and the volume scale only that drum (and every drum).
    ElectroDrums plain, louder, quieter;
    Settings up;
    up.drum(Drum::Snare).levelDb = 6.0;
    louder.setSettings(up);
    Settings down;
    down.volumeDb = -12.0;
    quieter.setSettings(down);
    const float base = peak(play(plain, {{62, 127, 0}}, 9600));
    CHECK(std::abs(peak(play(louder, {{62, 127, 0}}, 9600)) / base - 1.9953f) < 0.01f);
    CHECK(std::abs(peak(play(quieter, {{62, 127, 0}}, 9600)) / base - 0.2512f) < 0.01f);
    CHECK(peak(play(louder, {{60, 127, 0}}, 9600)) == peak(play(plain, {{60, 127, 0}}, 9600)));

    // Tune an octave up: the kick's body is about twice as high. Decay: the hit is longer.
    ElectroDrums tuned, longer;
    Settings t;
    t.drum(Drum::Kick).tuneSemitones = 12.0;
    tuned.setSettings(t);
    Settings l;
    l.drum(Drum::Kick).decay = 3.0;
    longer.setSettings(l);
    const auto kickPlain = play(plain, {{60, 127, 0}}, 48000);
    const auto kickTuned = play(tuned, {{60, 127, 0}}, 48000);
    const double ratio = frequencyBetween(kickTuned, 0.15, 0.3) / frequencyBetween(kickPlain, 0.15, 0.3);
    CHECK(ratio > 1.8 && ratio < 2.2);
    const auto kickLong = play(longer, {{60, 127, 0}}, 48000);
    CHECK(peak(kickLong, 24000) > 4.0f * peak(kickPlain, 24000));

    // The style: the same hit sounds different in 909 than in 808.
    ElectroDrums style909;
    Settings st;
    st.style = Style::Style909;
    style909.setSettings(st);
    CHECK(play(style909, {{60, 127, 0}}, 4800) != play(plain, {{60, 127, 0}}, 4800));
}

TEST(electrodrums, hits_are_counted_for_the_picture)
{
    ElectroDrums kit;
    CHECK(kit.hits(60).count == 0 && kit.hits(-1).count == 0 && kit.hits(200).count == 0);
    play(kit, {{60, 90, 0}, {60, 30, 100}, {63, 127, 200}, {59, 127, 300}}, 1000);
    CHECK(kit.hits(60).count == 2 && kit.hits(60).velocity == 30);
    CHECK(kit.hits(63).count == 1 && kit.hits(63).velocity == 127);
    CHECK(kit.hits(59).count == 0);                                    // no drum there
}
