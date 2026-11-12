#include "testkit.h"
#include "dsp/fft.h"
#include "instruments/bass/bassfretboard.h"
#include "instruments/bass/bassguitar.h"
#include "instruments/bass/basspickup.h"
#include "instruments/bass/basssettings.h"
#include "instruments/instrumentfactory.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

using namespace bass;

namespace {

constexpr double rate = 48000.0;

double keyHz(double key) { return 440.0 * std::exp2((key - 69.0) / 12.0); }
int frames(double seconds) { return int(seconds * rate); }

// The frequency of the loudest bin within 10 % of `expect`, refined between bins: a long
// Hann-windowed FFT from `from`.
double pitchNear(const std::vector<float>& v, std::size_t from, double expect)
{
    constexpr int n = 65536;
    const Fft fft(n);
    std::vector<std::complex<double>> bins(n);
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / n);
        bins[std::size_t(i)] = (from + std::size_t(i) < v.size() ? v[from + std::size_t(i)] : 0.0f) * w;
    }
    fft.forward(bins.data());
    const int lo = int(expect * 0.9 * n / rate), hi = int(expect * 1.1 * n / rate) + 1;
    int best = lo;
    for (int k = lo; k <= hi; ++k) if (std::abs(bins[std::size_t(k)]) > std::abs(bins[std::size_t(best)])) best = k;
    const double a = std::abs(bins[std::size_t(best - 1)]), b = std::abs(bins[std::size_t(best)]), c = std::abs(bins[std::size_t(best + 1)]);
    return (best + 0.5 * (a - c) / (a - 2.0 * b + c)) * rate / n;
}

double cents(double hz, double expect) { return 1200.0 * std::log2(hz / expect); }

double rms(const std::vector<float>& v, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) sum += double(v[i]) * v[i];
    return std::sqrt(sum / double(std::max<std::size_t>(1, to - from)));
}

double db(double gain) { return 20.0 * std::log10(gain + 1e-30); }

// The share of the energy above `hz` in v[from, from + 4096), in dB (a brightness measure).
double highShareDb(const std::vector<float>& v, std::size_t from, double hz)
{
    constexpr int n = 4096;
    const Fft fft(n);
    std::vector<std::complex<double>> bins(n);
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / n);
        bins[std::size_t(i)] = v[from + std::size_t(i)] * w;
    }
    fft.forward(bins.data());
    double high = 0.0, all = 0.0;
    for (int k = 1; k < n / 2; ++k) {
        const double p = std::norm(bins[std::size_t(k)]);
        all += p;
        if (k * rate / n >= hz) high += p;
    }
    return 10.0 * std::log10(high / (all + 1e-30) + 1e-30);
}

// A plain bass for measuring: no compressor or drive, the tone open.
Settings plain()
{
    Settings s;
    s.compression = 0.0;
    s.drive = 0.0;
    s.tone = 1.0;
    return s;
}

struct Run {
    BassGuitar bass;
    MidiBuffer midi;

    explicit Run(const Settings& s = plain())
    {
        bass.prepare(rate, 256);
        bass.setSettings(s);
        bass.reset();
    }
    void event(std::uint8_t status, std::uint8_t d1, std::uint8_t d2) { midi.add({0, status, d1, d2}); }
    void on(int key, int velocity = 100) { event(0x90, std::uint8_t(key), std::uint8_t(velocity)); }
    void off(int key) { event(0x80, std::uint8_t(key), 0); }
    // Renders `count` frames in 256-frame blocks; events go into the first block.
    std::vector<float> render(int count)
    {
        std::vector<float> left(static_cast<std::size_t>(count)), right(static_cast<std::size_t>(count));
        for (int pos = 0; pos < count; pos += 256) {
            const int n = std::min(256, count - pos);
            float* ch[2] = {left.data() + pos, right.data() + pos};
            ProcessContext c;
            c.audio = {ch, 2, n};
            c.midi = &midi;
            c.sampleRate = rate;
            bass.process(c);
            midi.clear();
        }
        return left;
    }
};

// One note held for `seconds`.
std::vector<float> note(int key, const Settings& s = plain(), int velocity = 100, double seconds = 2.0)
{
    Run run(s);
    run.on(key, velocity);
    return run.render(frames(seconds));
}

} // namespace

// ---------------------------------------------------------------- the string

TEST(bass, every_string_and_fret_plays_in_tune)
{
    for (const int key : {28, 33, 38, 43, 45, 50, 55, 62, 67}) {
        const auto v = note(key);
        const double c = cents(pitchNear(v, std::size_t(frames(0.1)), keyHz(key)), keyHz(key));
        if (std::abs(c) >= 3.0) std::printf("    key %d is %+.2f cents off\n", key, c);
        CHECK(std::abs(c) < 3.0);
    }
}

TEST(bass, the_tuning_holds_at_other_sample_rates)
{
    for (const double other : {44100.0, 96000.0}) {
        BassGuitar b;
        b.prepare(other, 256);
        b.setSettings(plain());
        b.reset();
        MidiBuffer midi;
        midi.add({0, 0x90, 33, 100});
        std::vector<float> out;
        std::vector<float> l(256), r(256);
        for (int block = 0; block < int(other * 1.6 / 256); ++block) {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            float* ch[2] = {l.data(), r.data()};
            ProcessContext c;
            c.audio = {ch, 2, 256};
            c.midi = &midi;
            c.sampleRate = other;
            b.process(c);
            midi.clear();
            out.insert(out.end(), l.begin(), l.end());
        }
        // Resample-free check: count the period by the FFT at this rate.
        constexpr int n = 65536;
        const Fft fft(n);
        std::vector<std::complex<double>> bins(n);
        for (int i = 0; i < n; ++i)
            bins[std::size_t(i)] = out[std::size_t(i) + 4800] * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / n));
        fft.forward(bins.data());
        const double expect = keyHz(33);
        const int lo = int(expect * 0.9 * n / other), hi = int(expect * 1.1 * n / other) + 1;
        int best = lo;
        for (int k = lo; k <= hi; ++k) if (std::abs(bins[std::size_t(k)]) > std::abs(bins[std::size_t(best)])) best = k;
        const double a = std::abs(bins[std::size_t(best - 1)]), m = std::abs(bins[std::size_t(best)]), c = std::abs(bins[std::size_t(best + 1)]);
        const double hz = (best + 0.5 * (a - c) / (a - 2.0 * m + c)) * other / n;
        CHECK(std::abs(cents(hz, expect)) < 3.0);
    }
}

TEST(bass, sustain_sets_how_long_a_string_rings)
{
    auto shortRing = plain(), longRing = plain();
    shortRing.sustain = 0.0;
    longRing.sustain = 1.0;
    const auto a = note(33, shortRing, 100, 3.0), b = note(33, longRing, 100, 3.0);
    const double dropShort = db(rms(a, frames(0.2), frames(0.4))) - db(rms(a, frames(2.0), frames(2.2)));
    const double dropLong = db(rms(b, frames(0.2), frames(0.4))) - db(rms(b, frames(2.0), frames(2.2)));
    CHECK(dropShort > dropLong + 15.0);
    // About the times the knob promises: 1.5 s and 12 s to -60 dB (low strings a little longer).
    CHECK(dropShort > 45.0);
    CHECK(dropLong < 20.0);
}

TEST(bass, brightness_keeps_the_overtones_going)
{
    auto dark = plain(), bright = plain();
    dark.brightness = 0.0;
    bright.brightness = 1.0;
    const auto a = note(38, dark), b = note(38, bright);
    CHECK(highShareDb(b, std::size_t(frames(0.5)), 600.0) > highShareDb(a, std::size_t(frames(0.5)), 600.0) + 10.0);
}

TEST(bass, a_released_key_damps_its_string)
{
    Run run;
    run.on(40);
    const auto held = run.render(frames(0.5));
    run.off(40);
    const auto after = run.render(frames(0.5));
    CHECK(db(rms(after, frames(0.3), frames(0.5))) < db(rms(held, frames(0.3), frames(0.5))) - 50.0);
    CHECK(run.bass.activeVoices() == 0);
    CHECK(run.bass.stringOf(40) == -1);
}

TEST(bass, palm_mute_makes_notes_short)
{
    auto muted = plain();
    muted.style = Style::Mute;
    muted.muteAmount = 1.0;
    const auto open = note(40), m = note(40, muted);
    const double openLevel = db(rms(open, frames(0.4), frames(0.6)));
    const double mutedLevel = db(rms(m, frames(0.4), frames(0.6)));
    CHECK(mutedLevel < openLevel - 30.0);
    // The attack is still there.
    CHECK(db(rms(m, 0, frames(0.05))) > db(rms(open, 0, frames(0.05))) - 10.0);
}

TEST(bass, slap_and_pop_are_brighter_than_fingers)
{
    auto slap = plain();
    slap.style = Style::Slap;
    for (const int key : {28, 45}) {                                    // a slap on E, a pop on D
        const auto finger = note(key, plain(), 110, 0.5), hit = note(key, slap, 110, 0.5);
        CHECK(highShareDb(hit, 0, 1500.0) > highShareDb(finger, 0, 1500.0) + 6.0);
    }
}

TEST(bass, the_pickups_and_tone_shape_the_sound)
{
    auto neck = plain(), bridge = plain(), closed = plain();
    neck.pickupBlend = 0.0;
    bridge.pickupBlend = 1.0;
    closed.tone = 0.0;
    const auto n = note(33, neck, 100, 1.0), b = note(33, bridge, 100, 1.0), c = note(33, closed, 100, 1.0);
    const auto from = std::size_t(frames(0.1));
    CHECK(highShareDb(b, from, 400.0) > highShareDb(n, from, 400.0) + 3.0);
    // The tone knob closed against the same pickups with it open.
    const auto open = note(33, plain(), 100, 1.0);
    CHECK(highShareDb(c, from, 1000.0) < highShareDb(open, from, 1000.0) - 15.0);
    // The blend changes the tone more than the level: within 6 dB.
    CHECK(std::abs(db(rms(b, from)) - db(rms(n, from))) < 6.0);
}

TEST(bass, softer_notes_are_quieter)
{
    const auto soft = note(40, plain(), 30, 0.5), loud = note(40, plain(), 127, 0.5);
    CHECK(db(rms(loud)) > db(rms(soft)) + 10.0);
    auto flat = plain();
    flat.velocity = 0.0;                                               // no velocity response
    CHECK(std::abs(db(rms(note(40, flat, 30, 0.5))) - db(rms(note(40, flat, 127, 0.5)))) < 1.0);
}

// ---------------------------------------------------------------- the neck

TEST(bass, keys_outside_the_neck_move_by_octaves)
{
    CHECK(foldIntoRange(28) == 28 && foldIntoRange(67) == 67);
    CHECK(foldIntoRange(24) == 36);                                     // C1 -> C2
    CHECK(foldIntoRange(27) == 39);
    CHECK(foldIntoRange(72) == 60);                                     // C5 -> C4
    CHECK(foldIntoRange(127) == 67);                                   // G9 -> G4
    CHECK(foldIntoRange(0) == 36);
}

TEST(bass, the_hand_picks_a_string_near_where_it_is)
{
    const std::array<bool, StringCount> none{};
    const std::array<unsigned long long, StringCount> since{};
    // Open strings when the note is one.
    CHECK(choose(28, none, since, 5.0).string == 0);
    CHECK(choose(43, none, since, 2.0).fret == 0);                     // open G near the nut
    CHECK(choose(43, none, since, 10.0).string == 1);                  // A string 10 up the neck
    // A2 (45): G string 2 near the nut, D string 7 in the middle, A string 12 up high.
    CHECK(choose(45, none, since, 2.0).string == 3);
    CHECK(choose(45, none, since, 7.0).string == 2);
    CHECK(choose(45, none, since, 12.0).string == 1);
    // A held string is left alone; with all held, the oldest is taken.
    std::array<bool, StringCount> held{};
    held[2] = true;
    CHECK(choose(45, held, since, 7.0).string != 2);
    std::array<bool, StringCount> all{true, true, true, true};
    const std::array<unsigned long long, StringCount> order{4, 9, 2, 7};
    CHECK(choose(45, all, order, 7.0).string == 2);
    CHECK(fretOn(0, 27) == -1 && fretOn(3, 68) == -1 && fretOn(1, 33) == 0);
}

TEST(bass, a_chord_goes_on_four_strings)
{
    Run run;
    for (const int key : {28, 40, 47, 55}) run.on(key, 100);
    run.render(frames(0.2));
    std::vector<int> strings;
    for (const int key : {28, 40, 47, 55}) strings.push_back(run.bass.stringOf(key));
    std::sort(strings.begin(), strings.end());
    CHECK((strings == std::vector<int>{0, 1, 2, 3}));
    CHECK(run.bass.activeVoices() == 4);
}

TEST(bass, a_string_plays_one_note_at_a_time)
{
    Run run;
    for (int i = 0; i < 6; ++i) { run.on(28); run.render(256); run.off(28); run.render(256); }
    CHECK(run.bass.activeVoices() <= 2);                               // the note and one fading
    run.on(28);
    run.render(frames(0.05));
    CHECK(run.bass.activeVoices() == 1);
}

// ---------------------------------------------------------------- styles and keyswitches

TEST(bass, keyswitches_change_the_style_until_the_page_does)
{
    Run run;
    CHECK(run.bass.currentStyle() == Style::Finger);
    run.on(SlapKeyswitch);
    run.off(SlapKeyswitch);
    run.render(256);
    CHECK(run.bass.currentStyle() == Style::Slap);
    CHECK(run.bass.activeVoices() == 0);                               // a keyswitch makes no sound
    run.on(MuteKeyswitch);
    run.render(256);
    CHECK(run.bass.currentStyle() == Style::Mute);
    run.on(FingerKeyswitch);
    run.render(256);
    CHECK(run.bass.currentStyle() == Style::Finger);

    run.on(SlapKeyswitch);
    run.render(256);
    auto page = plain();
    page.style = Style::Mute;                                          // the page's style wins
    run.bass.setSettings(page);
    run.render(256);
    CHECK(run.bass.currentStyle() == Style::Mute);
}

TEST(bass, with_keyswitches_off_those_keys_are_notes)
{
    auto s = plain();
    s.keyswitches = false;
    Run run(s);
    run.on(SlapKeyswitch);                                             // D0 -> D2 on the neck
    const auto v = run.render(frames(1.6));
    CHECK(run.bass.currentStyle() == Style::Finger);
    CHECK(run.bass.activeVoices() == 1);
    CHECK(std::abs(cents(pitchNear(v, std::size_t(frames(0.1)), keyHz(38)), keyHz(38))) < 3.0);
}

TEST(bass, hard_hits_slap_when_asked)
{
    auto s = plain();
    s.slapOnHardHits = true;
    s.slapVelocity = 100;
    const auto soft = note(28, s, 99, 0.5), hard = note(28, s, 100, 0.5);
    auto plainHard = plain();
    const auto finger = note(28, plainHard, 100, 0.5);
    CHECK(highShareDb(hard, 0, 1500.0) > highShareDb(finger, 0, 1500.0) + 6.0);
    CHECK(highShareDb(soft, 0, 1500.0) < highShareDb(hard, 0, 1500.0) - 6.0);
}

// ---------------------------------------------------------------- playing

TEST(bass, a_key_over_a_held_one_slides_there)
{
    auto s = plain();
    s.slideSeconds = 0.1;
    Run run(s);
    run.on(33);                                                        // open A
    run.render(frames(0.2));
    const int string = run.bass.stringOf(33);
    run.on(38);                                                        // D2: slides up the A string
    run.render(256);
    run.off(33);
    CHECK(run.bass.stringOf(38) == string);
    CHECK(run.bass.activeVoices() == 1);                               // no new pluck
    const auto v = run.render(frames(1.6));
    CHECK(std::abs(cents(pitchNear(v, std::size_t(frames(0.2)), keyHz(38)), keyHz(38))) < 3.0);

    auto never = plain();
    never.slideSeconds = 0.0;                                          // 0: a new note on another string
    Run plucked(never);
    plucked.on(33);
    plucked.render(frames(0.2));
    plucked.on(38);
    plucked.render(256);
    CHECK(plucked.bass.stringOf(38) != plucked.bass.stringOf(33));
    CHECK(plucked.bass.activeVoices() == 2);
}

TEST(bass, notes_struck_together_do_not_slide)
{
    Run run;
    run.on(33);
    run.on(40);
    run.render(256);
    CHECK(run.bass.stringOf(33) >= 0 && run.bass.stringOf(40) >= 0);
    CHECK(run.bass.stringOf(33) != run.bass.stringOf(40));
}

TEST(bass, pitch_bend_follows_the_bend_range)
{
    auto s = plain();
    s.bendSemitones = 5;
    Run run(s);
    run.event(0xE0, 0x7F, 0x7F);                                       // full up
    run.on(33);
    const auto v = run.render(frames(1.6));
    CHECK(std::abs(cents(pitchNear(v, std::size_t(frames(0.1)), keyHz(38)), keyHz(38))) < 5.0);
}

TEST(bass, sustain_pedal_lets_released_strings_ring)
{
    Run run;
    run.event(0xB0, 64, 127);
    run.on(40);
    run.render(256);
    run.off(40);
    run.render(frames(0.5));
    CHECK(run.bass.activeVoices() == 1);
    run.event(0xB0, 64, 0);
    run.render(frames(0.5));
    CHECK(run.bass.activeVoices() == 0);
}

TEST(bass, all_notes_off_damps_everything)
{
    Run run;
    for (const int key : {28, 40, 47}) run.on(key);
    run.render(256);
    run.event(0xB0, 123, 0);
    run.render(frames(0.5));
    CHECK(run.bass.activeVoices() == 0);
    for (const int key : {28, 40, 47}) CHECK(run.bass.stringOf(key) == -1);
}

TEST(bass, loud_settings_stay_finite_and_bounded)
{
    Settings s;
    s.style = Style::Slap;
    s.drive = 1.0;
    s.compression = 1.0;
    s.tone = 1.0;
    s.pickupBlend = 1.0;
    s.sustain = 1.0;
    s.brightness = 1.0;
    s.volumeDb = MaxVolumeDb;
    Run run(s);
    for (const int key : {28, 40, 47, 55, 67, 30}) run.on(key, 127);
    for (const float x : run.render(frames(1.0))) {
        CHECK(std::isfinite(x));
        if (!std::isfinite(x) || std::abs(x) > 4.0f) { CHECK(std::abs(x) <= 4.0f); break; }
    }
}

// ---------------------------------------------------------------- settings

TEST(bass, settings_round_trip_through_the_instrument)
{
    Settings s;                                                        // values a float holds exactly
    s.style = Style::Mute;
    s.muteAmount = 0.25;
    s.slapOnHardHits = true;
    s.slapVelocity = 90;
    s.keyswitches = false;
    s.pluckPosition = 0.75;
    s.sustain = 0.125;
    s.brightness = 0.875;
    s.velocity = 0.5;
    s.pickupBlend = 1.0;
    s.tone = 0.25;
    s.drive = 0.5;
    s.compression = 0.75;
    s.slideSeconds = 0.25;
    s.bendSemitones = 12;
    s.volumeDb = -6.0;
    BassGuitar b;
    b.setSettings(s);
    CHECK(b.settings() == s);
    CHECK_NEAR(BassGuitar().settings().pickupBlend, Settings{}.pickupBlend, 1e-6);
}

TEST(bass, sanitize_puts_everything_in_range)
{
    Settings s;
    s.style = Style(9);
    s.tone = std::numeric_limits<double>::quiet_NaN();
    s.drive = 3.0;
    s.slapVelocity = 0;
    s.slideSeconds = -1.0;
    s.bendSemitones = 40;
    s.volumeDb = std::numeric_limits<double>::infinity();
    const auto c = sanitize(s);
    CHECK(c.style == Style::Finger);
    CHECK(c.tone == Settings{}.tone);
    CHECK(c.drive == 1.0);
    CHECK(c.slapVelocity == 1);
    CHECK(c.slideSeconds == 0.0);
    CHECK(c.bendSemitones == MaxBendSemitones);
    CHECK(c.volumeDb == Settings{}.volumeDb);
    CHECK(sanitize(Settings{}) == Settings{});
}

TEST(bass, style_names_round_trip)
{
    for (int i = 0; i < 3; ++i) { Style v{}; CHECK(styleFromName(styleName(Style(i)), v) && v == Style(i)); }
    Style v{};
    CHECK(!styleFromName("tapping", v));
}

TEST(bass, bass_guitar_tracks_play_through_the_bass)
{
    const auto instrument = createInstrument("Bass Guitar");
    CHECK(instrument && instrument->typeId() == "bassguitar");
}

TEST(bass, every_setting_has_one_control_key_that_reads_and_writes_it)
{
    std::vector<std::string_view> keys;
    for (const auto& c : controls()) {
        CHECK(std::find(keys.begin(), keys.end(), c.key) == keys.end());
        keys.push_back(c.key);
        CHECK(findControl(c.key) == &c);
        CHECK(c.min <= c.max);
        if (c.kind == ControlKind::Choice) CHECK(c.max == double(c.choices.size() - 1));
        Settings s;
        const double was = c.get(s);
        CHECK(setControl(s, c.key, was == c.max ? c.min : c.max));
        CHECK(!(s == Settings{}));
        CHECK(sanitize(s) == s);
    }
    CHECK(keys.size() == 16);
}

TEST(bass, set_control_clamps_rounds_and_refuses_what_it_cannot_take)
{
    Settings s;
    CHECK(setControl(s, "tone", 7.0) && s.tone == 1.0);
    CHECK(setControl(s, "bendRange", 4.6) && s.bendSemitones == 5);
    CHECK(setControl(s, "slapOnHardHits", 0.9) && s.slapOnHardHits);
    CHECK(setControl(s, "slapVelocity", 300) && s.slapVelocity == 127);
    CHECK(!setControl(s, "tone", std::numeric_limits<double>::quiet_NaN()));
    CHECK(!setControl(s, "nothing", 1.0));
    CHECK(setChoice(s, "style", "slap") && s.style == Style::Slap);
    CHECK(!setChoice(s, "style", "tapping"));
    CHECK(!setChoice(s, "tone", "slap"));                              // not a choice
}
