#include "testkit.h"
#include "dsp/fft.h"
#include "engine/snapshotbuilder.h"
#include "instruments/harmonium/harmonium.h"
#include "instruments/instrumentfactory.h"
#include "session/project.h"
#include "instruments/harmonium/reedwave.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace {

constexpr double rate = 48000.0;
constexpr int fftSize = 16384;

// Hann-windowed magnitude spectrum of `samples` (fftSize of them), in dB, 0 dB = full-scale sine.
std::vector<double> spectrumDb(const std::vector<float>& samples)
{
    const Fft fft(fftSize);
    std::vector<std::complex<double>> bins(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / fftSize);
        bins[std::size_t(i)] = samples[std::size_t(i)] * w;
    }
    fft.forward(bins.data());
    std::vector<double> db(fftSize / 2);
    for (int k = 0; k < fftSize / 2; ++k)
        db[std::size_t(k)] = 20.0 * std::log10(std::abs(bins[std::size_t(k)]) * 4.0 / fftSize + 1e-15);
    return db;
}

// Loudest bin within +-3 bins of a frequency.
double levelAt(const std::vector<double>& db, double hz)
{
    const int centre = int(std::lround(hz * fftSize / rate));
    double best = -400.0;
    for (int k = centre - 3; k <= centre + 3; ++k)
        if (k >= 0 && k < int(db.size())) best = std::max(best, db[std::size_t(k)]);
    return best;
}

std::vector<float> play(reedwave::Timbre timbre, double hz, float pressure, int frames = fftSize)
{
    reedwave::Oscillator osc;
    osc.start(timbre, hz, rate);
    std::vector<float> out(static_cast<std::size_t>(frames));
    for (auto& v : out) v = osc.next(pressure);
    return out;
}

} // namespace

TEST(harmonium, reed_plays_at_its_pitch)
{
    for (const double hz : {65.41, 261.63, 1046.5}) {
        const auto db = spectrumDb(play(reedwave::Timbre::Diapason, hz, 1.0f));
        int peak = 1;
        for (int k = 1; k < fftSize / 2; ++k) if (db[std::size_t(k)] > db[std::size_t(peak)]) peak = k;
        CHECK_NEAR(peak * rate / fftSize, hz, rate / fftSize);    // the fundamental leads a diapason
    }
    reedwave::Oscillator osc;                                     // one period is 1 / f
    osc.start(reedwave::Timbre::Reed, 440.0, rate);
    CHECK_NEAR(osc.increment(), 440.0 / rate, 1e-15);
    osc.setFrequency(441.0, rate);
    CHECK_NEAR(osc.increment(), 441.0 / rate, 1e-15);
}

TEST(harmonium, reed_tables_hold_no_harmonic_past_the_limit)
{
    using reedwave::Tables;
    for (const double hz : {30.0, 110.0, 440.0, 1760.0, 4186.0, 9000.0}) {
        const int level = Tables::levelFor(hz, rate);
        CHECK(reedwave::HarmonicCounts[std::size_t(level)] * hz <= reedwave::TopHarmonicLimit * rate);
        if (level + 1 < int(reedwave::HarmonicCounts.size()))
            CHECK(reedwave::HarmonicCounts[std::size_t(level + 1)] * hz > reedwave::TopHarmonicLimit * rate);
    }
    CHECK(Tables::levelFor(30.0, rate) == int(reedwave::HarmonicCounts.size()) - 1);
    CHECK(Tables::levelFor(0.0, rate) == 0);

    // The top key of a harmonium and above: everything that is not a harmonic stays far down.
    for (const double hz : {2093.0, 4186.0, 7040.0}) {
        const auto db = spectrumDb(play(reedwave::Timbre::Reed, hz, 1.0f));
        double peak = -400.0, stray = -400.0;
        for (int k = 4; k < fftSize / 2; ++k) {
            const double f = k * rate / fftSize;
            const double nearest = std::round(f / hz) * hz;
            peak = std::max(peak, db[std::size_t(k)]);
            if (std::abs(f - nearest) > 40.0) stray = std::max(stray, db[std::size_t(k)]);
        }
        CHECK(stray < peak - 70.0);                               // no folded-back overtones
    }
}

TEST(harmonium, indian_reed_overtones_fall_away_and_western_stops_are_rounder)
{
    // Single notes of the references: the overtones rise to about the 3rd, then fall away
    // steadily (an even, flat stack of overtones sounds like a buzz-saw, not a reed).
    const double hz = 220.0;
    const auto reed = spectrumDb(play(reedwave::Timbre::Reed, hz, 1.0f));
    const double third = levelAt(reed, 3 * hz);
    // The bare reed; the case's high cut takes the far overtones further down.
    for (int h = 1; h <= 6; ++h) CHECK(levelAt(reed, h * hz) > third - 9.0);   // a full body
    CHECK(levelAt(reed, 10 * hz) < third - 10.0);
    for (int h = 16; h <= 24; ++h) CHECK(levelAt(reed, h * hz) < third - 24.0);

    const auto diapason = spectrumDb(play(reedwave::Timbre::Diapason, hz, 1.0f));
    const auto dulciana = spectrumDb(play(reedwave::Timbre::Dulciana, hz, 1.0f));
    const auto bourdon = spectrumDb(play(reedwave::Timbre::Bourdon, hz, 1.0f));
    const auto brightness = [&](const std::vector<double>& db) { return levelAt(db, 8 * hz) - levelAt(db, hz); };
    CHECK(brightness(reed) > brightness(diapason) + 10.0);
    CHECK(brightness(diapason) > brightness(dulciana) + 5.0);
    CHECK(levelAt(bourdon, 2 * hz) < levelAt(bourdon, 3 * hz));   // weak even harmonics
}

TEST(harmonium, low_pressure_takes_the_overtones_away)
{
    const double hz = 220.0;
    const auto soft = spectrumDb(play(reedwave::Timbre::Reed, hz, 0.0f));
    const auto half = spectrumDb(play(reedwave::Timbre::Reed, hz, 0.5f));
    const auto full = spectrumDb(play(reedwave::Timbre::Reed, hz, 1.0f));
    const auto top = [&](const std::vector<double>& db) { return levelAt(db, 8 * hz) - levelAt(db, 2 * hz); };
    CHECK(top(soft) < top(half) && top(half) < top(full));
    CHECK(top(full) - top(soft) > 10.0);
}

TEST(harmonium, loudness_stays_even_from_bass_to_treble)
{
    const auto rmsDb = [](const std::vector<float>& v) {
        double sum = 0.0;
        for (const float x : v) sum += double(x) * x;
        return 10.0 * std::log10(sum / double(v.size()));
    };
    const double low = rmsDb(play(reedwave::Timbre::Diapason, 65.41, 1.0f, 48000));
    const double mid = rmsDb(play(reedwave::Timbre::Diapason, 523.25, 1.0f, 48000));
    CHECK_NEAR(low, -12.0, 0.5);                                  // the richest table, as scaled
    CHECK(std::abs(mid - low) < 1.5);                             // losing top harmonics only
    for (const float x : play(reedwave::Timbre::Reed, 110.0, 1.0f, 48000)) CHECK(std::abs(x) < 1.0f);
}


// ---------------------------------------------------------------- Harmonium

namespace {

struct Run {
    Harmonium h;
    MidiBuffer midi;
    explicit Run(harmonium::Settings s = quiet())
    {
        h.prepare(rate, 256);
        h.setSettings(s);
        h.reset();                                               // start at the set pressure
    }
    // No pumping, air or click: only the reeds, for measurements.
    static harmonium::Settings quiet()
    {
        harmonium::Settings s;
        s.pumping = 0.0; s.airNoise = 0.0; s.keyClick = 0.0; s.beatingCents = 0.0;
        s.sustainSeconds = 0.0;                                  // a plain key release
        return s;
    }
    void event(int offset, std::uint8_t status, std::uint8_t d1, std::uint8_t d2) { midi.add({offset, status, d1, d2}); }
    // Renders `frames` in 256-frame blocks; events go into the first block.
    std::vector<float> render(int frames, int channels = 2)
    {
        std::vector<float> out(static_cast<std::size_t>(frames)), right(static_cast<std::size_t>(frames));
        for (int pos = 0; pos < frames; pos += 256) {
            const int n = std::min(256, frames - pos);
            float* ch[2] = {out.data() + pos, right.data() + pos};
            ProcessContext c;
            c.audio = {ch, channels, n};
            c.midi = &midi;
            c.sampleRate = rate;
            h.process(c);
            midi.clear();
        }
        return out;
    }
};

double rmsDb(const std::vector<float>& v, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) sum += double(v[i]) * v[i];
    return 10.0 * std::log10(sum / double(std::max<std::size_t>(1, to - from)) + 1e-30);
}

std::vector<float> tail(const std::vector<float>& v, int frames)
{
    return {v.end() - frames, v.end()};
}

double hz(int key) { return 440.0 * std::exp2((key - 69) / 12.0); }

// Level of harmonic lines of f against the level half-way between them.
double harmonicContrast(const std::vector<double>& db, double f, int count)
{
    double on = 0.0, off = 0.0;
    for (int k = 1; k <= count; ++k) { on += levelAt(db, k * f); off += levelAt(db, (k + 0.5) * f); }
    return (on - off) / count;
}

} // namespace

TEST(harmonium, note_starts_on_its_frame_ignores_velocity_and_stops_after_release)
{
    Run soft, loud;
    soft.event(100, 0x90, 60, 10);
    loud.event(100, 0x90, 60, 127);
    const auto a = soft.render(24000), b = loud.render(24000);
    for (int i = 0; i < 100; ++i) CHECK(a[std::size_t(i)] == 0.0f);
    CHECK(std::abs(a[200]) > 0.0f);
    CHECK_NEAR(rmsDb(a, 12000), rmsDb(b, 12000), 0.01);          // no key-speed velocity
    CHECK(rmsDb(a, 12000) > -40.0 && rmsDb(a, 12000) < -10.0);

    const auto db = spectrumDb(tail(b, fftSize));
    CHECK(harmonicContrast(db, hz(60), 8) > 30.0);               // a clean C4 harmonic series

    soft.event(0, 0x80, 60, 0);
    const auto after = soft.render(24000);
    CHECK(rmsDb(after, 0, 480) > -40.0);                        // the reed rings briefly
    CHECK(rmsDb(after, 19200) < -120.0);                         // then nothing: no air, no reeds
    CHECK(soft.h.activeVoices() == 0);
}

static harmonium::Settings withSustain(double seconds)
{
    auto s = Run::quiet();
    s.sustainSeconds = seconds;
    return s;
}

static int frames(double seconds) { return int(seconds * rate); }

TEST(harmonium, new_harmonium_sustains_for_one_and_a_half_seconds)
{
    CHECK_NEAR(harmonium::Settings{}.sustainSeconds, 1.5, 1e-9);
    Harmonium h;
    CHECK_NEAR(h.settings().sustainSeconds, 1.5, 1e-6);
    harmonium::Settings wild;
    wild.sustainSeconds = 99.0;
    CHECK_NEAR(harmonium::sanitize(wild).sustainSeconds, harmonium::MaxSustainSeconds, 1e-9);
}

TEST(harmonium, sustain_keeps_a_released_key_ringing_then_it_fades_away)
{
    Run run(withSustain(1.0));                                   // full for 0.5 s, fade over 1 s
    run.event(0, 0x90, 60, 100);
    const auto held = run.render(frames(0.3));
    const double heldDb = rmsDb(held, std::size_t(frames(0.2)));
    run.event(0, 0x80, 60, 0);
    const auto after = run.render(frames(1.7));
    CHECK(std::abs(rmsDb(after, 0, std::size_t(frames(0.4))) - heldDb) < 1.0);   // still at full level
    const double fading = rmsDb(after, std::size_t(frames(0.9)), std::size_t(frames(1.0)));
    CHECK(fading < heldDb - 15.0 && fading > heldDb - 60.0);    // dying slowly, not cut
    CHECK(run.h.activeVoices() == 0);                            // gone after 1.5 s
}

TEST(harmonium, the_next_key_cuts_a_sustaining_one)
{
    Run run(withSustain(2.0));
    run.event(0, 0x90, 60, 100);
    run.render(frames(0.2));
    run.event(0, 0x80, 60, 0);
    run.render(frames(0.3));
    CHECK(run.h.activeVoices() == 1);                            // C still rings
    run.event(0, 0x90, 64, 100);
    run.render(frames(0.4));
    CHECK(run.h.activeVoices() == 1);                            // only E now
    run.event(0, 0x90, 64, 100);                                 // the same key again
    run.event(0, 0x80, 64, 0);
    run.render(frames(0.1));
    run.event(0, 0x90, 64, 100);                                 // pressed while it sustains
    const auto again = run.render(frames(0.2));
    CHECK(run.h.activeVoices() == 1);
    CHECK(rmsDb(again, 0, 480) > -40.0);                         // picked up, no gap
}

TEST(harmonium, legato_playing_does_not_sustain_the_key_left_behind)
{
    Run run(withSustain(2.0));
    run.event(0, 0x90, 60, 100);
    run.render(frames(0.3));
    run.event(0, 0x90, 62, 100);                                 // D goes down before C comes up
    run.render(frames(0.2));
    run.event(0, 0x80, 60, 0);
    run.render(frames(0.4));
    CHECK(run.h.activeVoices() == 1);                            // C ended at once, D sounds

    run.event(0, 0x80, 62, 0);                                   // the last key sustains
    run.render(frames(0.3));
    CHECK(run.h.activeVoices() == 1);
}

TEST(harmonium, a_chord_let_go_note_by_note_sustains_whole)
{
    Run run(withSustain(2.0));
    run.event(0, 0x90, 60, 100);
    run.event(frames(0.02), 0x90, 64, 100);                      // a slightly rolled chord
    run.event(frames(0.04), 0x90, 67, 100);
    run.render(frames(0.3));
    run.event(0, 0x80, 60, 0);
    run.render(frames(0.05));
    run.event(0, 0x80, 64, 0);
    run.render(frames(0.05));
    run.event(0, 0x80, 67, 0);
    run.render(frames(0.3));
    CHECK(run.h.activeVoices() == 3);
    run.event(0, 0xB0, 123, 0);                                  // all notes off ends the tails
    run.render(frames(0.5));
    CHECK(run.h.activeVoices() == 0);
}

TEST(harmonium, repeated_key_keeps_sounding_and_voices_are_bounded)
{
    Run run;
    run.event(0, 0x90, 64, 100);
    auto a = run.render(9600);
    run.event(0, 0x90, 64, 100);                                 // again, without a note-off
    auto b = run.render(9600);
    CHECK(run.h.activeVoices() == 1);
    CHECK(std::abs(rmsDb(b, 0, 480) - rmsDb(a, 4800, 9600)) < 1.0);   // no dip, no re-attack

    for (int key = 30; key < 100; ++key) run.event(0, 0x90, std::uint8_t(key), 100);
    const auto big = run.render(4800);
    CHECK(run.h.activeVoices() <= Harmonium::Polyphony + 16);
    for (const float x : big) CHECK(std::isfinite(x) && std::abs(x) < 4.0f);
    run.event(0, 0xB0, 120, 0);                                  // all sound off
    run.render(256);
    CHECK(run.h.activeVoices() == 0);
}

TEST(harmonium, stops_change_held_notes_and_bass_adds_the_octave_below)
{
    Run run;
    run.event(0, 0x90, 57, 100);                                 // A3, male reeds only
    auto male = run.render(fftSize * 2);
    const double f = hz(57);
    auto db = spectrumDb(tail(male, fftSize));
    const double belowBefore = levelAt(db, f / 2.0);

    auto s = run.h.settings();
    s.indian.bass = true;                                        // pull the bass stop, key held
    run.h.setSettings(s);
    auto both = run.render(fftSize * 2);
    db = spectrumDb(tail(both, fftSize));
    CHECK(levelAt(db, f / 2.0) > belowBefore + 30.0);
    CHECK(levelAt(db, 1.5 * f) > levelAt(db, 1.25 * f) + 20.0);  // the bass reed's own series

    s.indian.male = false; s.indian.bass = false;                // every stop closed: silence
    run.h.setSettings(s);
    const auto none = run.render(9600);
    CHECK(rmsDb(none, 7200) < -100.0);                          // closed within ~110 ms
}

TEST(harmonium, beating_between_reed_sets_makes_the_level_waver)
{
    const auto waver = [](double cents) {
        auto s = Run::quiet();
        s.indian.female = true;
        s.beatingCents = cents;
        Run run(s);
        run.event(0, 0x90, 57, 100);
        const auto out = run.render(int(rate * 3));
        std::vector<double> levels;                              // 20 ms levels over the last 2 s
        for (int i = int(rate); i + 960 <= int(out.size()); i += 960) levels.push_back(rmsDb(out, std::size_t(i), std::size_t(i + 960)));
        return *std::max_element(levels.begin(), levels.end()) - *std::min_element(levels.begin(), levels.end());
    };
    CHECK(waver(0.0) < 1.0);
    CHECK(waver(10.0) > 2.0);
}

TEST(harmonium, bellows_and_expression_set_level_and_brightness)
{
    const auto play = [](double bellows, int expression) {
        auto s = Run::quiet();
        s.bellows = bellows;
        Run run(s);
        run.event(0, 0xB0, 11, std::uint8_t(expression));
        run.event(0, 0x90, 57, 100);
        return run.render(fftSize * 2);
    };
    const auto low = play(0.3, 127), high = play(0.9, 127);
    CHECK(rmsDb(high, fftSize) > rmsDb(low, fftSize) + 6.0);
    const auto lowDb = spectrumDb(tail(low, fftSize)), highDb = spectrumDb(tail(high, fftSize));
    const double f = hz(57);
    CHECK(levelAt(highDb, 8 * f) - levelAt(highDb, 2 * f) > levelAt(lowDb, 8 * f) - levelAt(lowDb, 2 * f) + 4.0);

    CHECK(rmsDb(play(0.0, 127), fftSize) < -100.0);              // no air, no sound
    CHECK(rmsDb(play(0.9, 0), fftSize) < -100.0);                // expression pedal shut
}

TEST(harmonium, scale_changer_moves_keys_and_note_off_still_finds_them)
{
    auto s = Run::quiet();
    s.transpose = 2;
    Run run(s);
    run.event(0, 0x90, 60, 100);
    const auto out = run.render(fftSize * 2);
    const auto db = spectrumDb(tail(out, fftSize));
    CHECK(harmonicContrast(db, hz(62), 8) > 30.0);               // sounds D4
    s.transpose = 0;                                             // changed while held
    run.h.setSettings(s);
    run.event(0, 0x80, 60, 0);
    run.render(24000);
    CHECK(run.h.activeVoices() == 0);
}

TEST(harmonium, drones_sound_without_keys_until_turned_off)
{
    auto s = Run::quiet();
    s.drones = {48, 55, harmonium::NoDrone, harmonium::NoDrone};  // Sa and Pa
    Run run(s);
    const auto out = run.render(fftSize * 2);
    CHECK(run.h.activeVoices() == 2);
    const auto db = spectrumDb(tail(out, fftSize));
    CHECK(levelAt(db, hz(48) * 2) > -60.0 && levelAt(db, hz(55) * 2) > -60.0);
    run.event(0, 0xB0, 123, 0);                                  // all notes off leaves drones
    run.render(4800);
    CHECK(run.h.activeVoices() == 2);
    s.drones = {harmonium::NoDrone, harmonium::NoDrone, harmonium::NoDrone, harmonium::NoDrone};
    run.h.setSettings(s);
    const auto off = run.render(24000);
    CHECK(run.h.activeVoices() == 0);
    CHECK(rmsDb(off, 19200) < -120.0);
}

TEST(harmonium, a_drone_sits_softer_and_darker_than_the_same_key_played)
{
    auto s = Run::quiet();
    Run played(s);
    played.event(0, 0x90, 48, 100);
    s.drones = {48, harmonium::NoDrone, harmonium::NoDrone, harmonium::NoDrone};
    Run drone(s);
    const auto key = played.render(fftSize * 2), held = drone.render(fftSize * 2);
    const double drop = rmsDb(tail(key, fftSize)) - rmsDb(tail(held, fftSize));
    CHECK(drop > 3.0 && drop < 10.0);
    const auto kdb = spectrumDb(tail(key, fftSize)), ddb = spectrumDb(tail(held, fftSize));
    const double f = hz(48);
    const auto upper = [&](const std::vector<double>& db) { return levelAt(db, 8 * f) - levelAt(db, f); };
    CHECK(upper(ddb) < upper(kdb) - 3.0);                         // fewer high overtones
}

TEST(harmonium, case_reflections_are_darker_than_the_dry_sound)
{
    auto s = Run::quiet();
    s.caseResonance = 1.0;
    s.indian.female = true;
    Harmonium h;
    h.prepare(rate, 256);
    h.setSettings(s);
    h.reset();
    MidiBuffer midi;
    midi.add({0, 0x90, 57, 100});
    const int total = fftSize * 2;
    std::vector<float> l(static_cast<std::size_t>(total)), r(static_cast<std::size_t>(total));
    for (int pos = 0; pos < total; pos += 256) {
        float* ch[2] = {l.data() + pos, r.data() + pos};
        ProcessContext c;
        c.audio = {ch, 2, 256};
        c.midi = &midi;
        c.sampleRate = rate;
        h.process(c);
        midi.clear();
    }
    // The dry sound is the same on both sides, so L - R holds only the reflections.
    std::vector<float> mid(static_cast<std::size_t>(fftSize)), side(static_cast<std::size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i) {
        const auto k = std::size_t(total - fftSize + i);
        mid[std::size_t(i)] = l[k] + r[k];
        side[std::size_t(i)] = l[k] - r[k];
    }
    const auto mdb = spectrumDb(mid), sdb = spectrumDb(side);
    const double f = hz(57);
    // Energy of the harmonics from 2.5 to 5 kHz against those from 200 to 800 Hz.
    const auto tilt = [&](const std::vector<double>& db) {
        double high = 0.0, low = 0.0;
        int nh = 0, nl = 0;
        for (int k = 1; k * f < 5000.0; ++k) {
            const double x = levelAt(db, k * f);
            if (k * f >= 2500.0) { high += x; ++nh; }
            else if (k * f >= 200.0 && k * f <= 800.0) { low += x; ++nl; }
        }
        return high / std::max(nh, 1) - low / std::max(nl, 1);
    };
    CHECK(tilt(sdb) < tilt(mdb) - 8.0);                           // wood swallows the highs
}

TEST(harmonium, western_style_is_rounder_and_vox_humana_trembles)
{
    auto s = Run::quiet();
    s.style = harmonium::Style::Western;
    Run west(s), indian;
    west.event(0, 0x90, 57, 100);
    indian.event(0, 0x90, 57, 100);
    const auto w = spectrumDb(tail(west.render(fftSize * 2), fftSize));
    const auto i = spectrumDb(tail(indian.render(fftSize * 2), fftSize));
    const double f = hz(57);
    CHECK(levelAt(w, f) > levelAt(w, 2 * f));                    // fundamental leads
    CHECK(levelAt(w, 6 * f) - levelAt(w, f) < levelAt(i, 6 * f) - levelAt(i, f) - 10.0);

    s.western.voxHumana = true;
    Run vox(s);
    vox.event(0, 0x90, 57, 100);
    const auto out = vox.render(int(rate * 2));
    std::vector<double> levels;
    for (int k = int(rate); k + 480 <= int(out.size()); k += 480) levels.push_back(rmsDb(out, std::size_t(k), std::size_t(k + 480)));
    CHECK(*std::max_element(levels.begin(), levels.end()) - *std::min_element(levels.begin(), levels.end()) > 2.0);
}

TEST(harmonium, mono_buffer_gets_the_sum_and_settings_round_trip)
{
    Run stereo, mono;
    stereo.event(0, 0x90, 60, 100);
    mono.event(0, 0x90, 60, 100);
    const auto l = stereo.render(4800), m = mono.render(4800, 1);
    CHECK(rmsDb(m, 2400) > rmsDb(l, 2400) - 1.0);

    harmonium::Settings s;
    s.style = harmonium::Style::Western;
    s.western = {true, false, true, true, true};
    s.bellows = 0.5; s.pumping = 0.25; s.beatingCents = 7.5; s.airNoise = 0.75;
    s.keyClick = 0.5; s.caseResonance = 0.25; s.transpose = -3; s.volumeDb = -6.0;
    s.drones = {48, harmonium::NoDrone, 55, 60};
    Harmonium h;
    h.setSettings(s);
    CHECK(h.settings() == s);
    CHECK(h.typeId() == "harmonium");

    harmonium::Settings bad;
    bad.bellows = 3.0; bad.beatingCents = std::nan(""); bad.transpose = 40; bad.drones = {200, -7, 64, -1};
    const auto clean = harmonium::sanitize(bad);
    CHECK(clean.bellows == 1.0 && clean.beatingCents == harmonium::Settings{}.beatingCents);
    CHECK(clean.transpose == harmonium::MaxTranspose);
    CHECK(clean.drones[0] == harmonium::NoDrone && clean.drones[1] == harmonium::NoDrone && clean.drones[2] == 64);
}

TEST(harmonium, harmonium_tracks_get_the_harmonium_and_their_settings_reach_it)
{
    CHECK(createInstrument("Harmonium")->typeId() == "harmonium");
    CHECK(createInstrument("Piano")->typeId() == "testsynth");     // the others are not built yet

    Project project;
    auto& track = project.addTrack(TrackKind::Instrument, "Harmonium", "Harmonium");
    auto settings = harmonium::Settings{};
    settings.indian.female = true;
    settings.transpose = 3;
    settings.drones = {50, harmonium::NoDrone, harmonium::NoDrone, harmonium::NoDrone};
    CHECK(project.setTrackHarmonium(track.id, settings));

    std::unordered_map<int, SnapshotBuilder::TrackEffects> effects;
    effects[track.id].instrument = createInstrument(track.type);
    SnapshotBuilder::syncTrackEq(project, effects);
    // The processor keeps its knobs as floats, so 0.7 comes back as 0.69999999.
    auto applied = static_cast<Harmonium&>(*effects[track.id].instrument).settings();
    CHECK_NEAR(applied.bellows, settings.bellows, 1e-6);
    CHECK_NEAR(applied.pumping, settings.pumping, 1e-6);
    applied.bellows = settings.bellows; applied.pumping = settings.pumping;
    applied.airNoise = settings.airNoise; applied.keyClick = settings.keyClick;
    CHECK(applied == settings);

    auto& piano = project.addTrack(TrackKind::Instrument, "Keys", "Piano");   // not touched
    effects[piano.id].instrument = createInstrument(piano.type);
    SnapshotBuilder::syncTrackEq(project, effects);
    CHECK(effects[piano.id].instrument->typeId() == "testsynth");
}
