#include "testkit.h"
#include "dsp/fft.h"
#include "instruments/instrumentfactory.h"
#include "instruments/synth/synth.h"
#include "instruments/synth/synthenvelope.h"
#include "instruments/synth/synthfilter.h"
#include "instruments/synth/synthlfo.h"
#include "instruments/synth/synthoscillator.h"
#include "instruments/synth/synthsettings.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

using namespace synth;

namespace {

constexpr double rate = 48000.0;
constexpr int fftSize = 16384;

// Hann-windowed magnitude spectrum of the first fftSize samples from `from`, in dB.
std::vector<double> spectrumDb(const std::vector<float>& samples, std::size_t from = 0)
{
    const Fft fft(fftSize);
    std::vector<std::complex<double>> bins(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / fftSize);
        bins[std::size_t(i)] = samples[from + std::size_t(i)] * w;
    }
    fft.forward(bins.data());
    std::vector<double> db(fftSize / 2);
    for (int k = 0; k < fftSize / 2; ++k)
        db[std::size_t(k)] = 20.0 * std::log10(std::abs(bins[std::size_t(k)]) * 4.0 / fftSize + 1e-15);
    return db;
}

// The frequency of the loudest bin between lo and hi Hz.
double peakHz(const std::vector<double>& db, double lo = 20.0, double hi = 20000.0)
{
    const int a = std::max(1, int(lo * fftSize / rate)), b = std::min(int(db.size()) - 1, int(hi * fftSize / rate));
    int best = a;
    for (int k = a; k <= b; ++k) if (db[std::size_t(k)] > db[std::size_t(best)]) best = k;
    return best * rate / fftSize;
}

double rms(const std::vector<float>& v, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) sum += double(v[i]) * v[i];
    return std::sqrt(sum / double(std::max<std::size_t>(1, to - from)));
}

double db(double gain) { return 20.0 * std::log10(gain + 1e-30); }
double keyHz(double key) { return 440.0 * std::exp2((key - 69.0) / 12.0); }

// A filter's gain at `hz`, in dB: a sine through it, the settled half measured.
double filterGainDb(FilterMode mode, double cutoff, double resonance, double hz)
{
    SvfFilter f;
    f.set(mode, cutoff, resonance, rate);
    const int n = 48000;
    std::vector<float> in(static_cast<std::size_t>(n)), out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        in[std::size_t(i)] = float(std::sin(2.0 * std::numbers::pi * hz * i / rate));
        out[std::size_t(i)] = f.process(in[std::size_t(i)]);
    }
    return db(rms(out, n / 2) / rms(in, n / 2));
}

// Energy off the harmonics of f (more than 3 bins from any), against the total, in dB.
double aliasDb(const std::vector<float>& samples, double f)
{
    const auto spectrum = spectrumDb(samples);
    double on = 0.0, off = 0.0;
    for (int k = 1; k < int(spectrum.size()); ++k) {
        const double hz = k * rate / fftSize;
        const double harmonic = std::round(hz / f) * f;
        const double p = std::pow(10.0, spectrum[std::size_t(k)] / 10.0);
        if (std::abs(hz - harmonic) * fftSize / rate <= 3.0) on += p;
        else off += p;
    }
    return 10.0 * std::log10(off / (on + off));
}

std::vector<float> playWave(Wave wave, double hz, double width = 0.5, int frames = fftSize)
{
    WaveOscillator osc;
    std::vector<float> out(static_cast<std::size_t>(frames));
    for (auto& x : out) x = osc.next(wave, hz / rate, width);
    return out;
}

// A plain sine voice with the filter wide open and no envelope shaping, for pitch checks.
Settings plain()
{
    Settings s;
    s.osc[0].wave = Wave::Sine;
    s.osc[1].on = false;
    s.filter.mode = FilterMode::LowPass12;
    s.filter.cutoffHz = MaxCutoffHz;
    s.filter.keyTrack = 0.0;
    s.filter.envOctaves = 0.0;
    s.ampEnv = {0.001, 0.001, 1.0, 0.05};
    s.ampVelocity = 0.0;
    return s;
}

struct Run {
    Synth synth;
    MidiBuffer midi;
    double bpm = 120.0, beat = 0.0;
    bool playing = false;

    explicit Run(const Settings& s = plain())
    {
        synth.prepare(rate, 256);
        synth.setSettings(s);
        synth.reset();
    }
    void event(std::uint8_t status, std::uint8_t d1, std::uint8_t d2) { midi.add({0, status, d1, d2}); }
    void on(int key, int velocity = 100) { event(0x90, std::uint8_t(key), std::uint8_t(velocity)); }
    void off(int key) { event(0x80, std::uint8_t(key), 0); }
    // Renders `frames` in 256-frame blocks; events go into the first block. A playing song
    // moves `beat` on with the frames.
    std::vector<float> render(int frames)
    {
        std::vector<float> left(static_cast<std::size_t>(frames)), right(static_cast<std::size_t>(frames));
        for (int pos = 0; pos < frames; pos += 256) {
            const int n = std::min(256, frames - pos);
            float* ch[2] = {left.data() + pos, right.data() + pos};
            ProcessContext c;
            c.audio = {ch, 2, n};
            c.midi = &midi;
            c.sampleRate = rate;
            c.bpm = bpm;
            c.beat = beat;
            c.playing = playing;
            synth.process(c);
            midi.clear();
            if (playing) beat += n / rate * bpm / 60.0;
        }
        return left;
    }
};

int frames(double seconds) { return int(seconds * rate); }

} // namespace

// ---------------------------------------------------------------- oscillator

TEST(synth, every_wave_plays_at_its_pitch)
{
    for (const auto wave : {Wave::Saw, Wave::Pulse, Wave::Triangle, Wave::Sine})
        for (const double hz : {110.0, 440.0, 1760.0})
            CHECK_NEAR(peakHz(spectrumDb(playWave(wave, hz))), hz, 3.0);
}

TEST(synth, polyblep_takes_out_most_of_the_aliasing)
{
    // A high saw and pulse: the plain jump folds whistles back below Nyquist.
    const double hz = 3520.0;
    for (const auto wave : {Wave::Saw, Wave::Pulse}) {
        WaveOscillator osc;
        std::vector<float> naive(fftSize);
        double phase = 0.0;
        for (auto& x : naive) {
            x = wave == Wave::Saw ? float(2.0 * phase - 1.0) : (phase < 0.5 ? 1.0f : -1.0f);
            phase += hz / rate;
            phase -= std::floor(phase);
        }
        const double plainAlias = aliasDb(naive, hz);
        const double blepAlias = aliasDb(playWave(wave, hz), hz);
        CHECK(blepAlias < plainAlias - 10.0);
    }
}

TEST(synth, pulse_width_sets_the_duty_and_leaves_no_dc)
{
    for (const double width : {0.1, 0.5, 0.8}) {
        const auto v = playWave(Wave::Pulse, 100.0, width, 48000);
        double sum = 0.0;
        int high = 0;
        for (const float x : v) { sum += x; high += x > 0.0f ? 1 : 0; }
        CHECK_NEAR(sum / double(v.size()), 0.0, 0.01);
        CHECK_NEAR(double(high) / double(v.size()), width, 0.01);
    }
}

// ---------------------------------------------------------------- filter

TEST(synth, low_pass_slopes_are_12_and_24_db_per_octave)
{
    // Two octaves above a 1 kHz cutoff: about -24 dB for one stage, about -48 dB for two.
    const double lp12 = filterGainDb(FilterMode::LowPass12, 1000.0, 0.0, 4000.0);
    const double lp24 = filterGainDb(FilterMode::LowPass24, 1000.0, 0.0, 4000.0);
    CHECK(lp12 < -20.0 && lp12 > -28.0);
    CHECK(lp24 < -42.0);
    // Two octaves below: through.
    CHECK_NEAR(filterGainDb(FilterMode::LowPass12, 1000.0, 0.0, 250.0), 0.0, 0.5);
    CHECK_NEAR(filterGainDb(FilterMode::LowPass24, 1000.0, 0.0, 250.0), 0.0, 1.0);
}

TEST(synth, high_and_band_pass_keep_their_side)
{
    CHECK(filterGainDb(FilterMode::HighPass, 1000.0, 0.0, 250.0) < -20.0);
    CHECK_NEAR(filterGainDb(FilterMode::HighPass, 1000.0, 0.0, 8000.0), 0.0, 0.5);
    CHECK_NEAR(filterGainDb(FilterMode::BandPass, 1000.0, 0.0, 1000.0), 0.0, 0.5);
    CHECK(filterGainDb(FilterMode::BandPass, 1000.0, 0.0, 100.0) < -15.0);
    CHECK(filterGainDb(FilterMode::BandPass, 1000.0, 0.0, 10000.0) < -15.0);
}

TEST(synth, resonance_peaks_at_the_cutoff)
{
    const double flat = filterGainDb(FilterMode::LowPass12, 1000.0, 0.0, 1000.0);
    const double peaked = filterGainDb(FilterMode::LowPass12, 1000.0, 0.9, 1000.0);
    CHECK(flat < -2.0);                                  // the plain corner is 3 dB down
    CHECK(peaked > 10.0);
}

// ---------------------------------------------------------------- envelope

TEST(synth, envelope_attack_decay_and_release_take_their_time)
{
    AdsrEnvelope e;
    e.configure({0.01, 0.1, 0.5, 0.2}, rate);
    e.gateOn();
    int n = 0;
    while (e.stage() == AdsrEnvelope::Stage::Attack) { e.next(); ++n; }
    CHECK_NEAR(n, frames(0.01), 2);

    for (int i = 0; i < frames(0.1); ++i) e.next();      // 1 % of the way left
    CHECK_NEAR(e.level(), 0.5 + 0.005, 0.002);
    for (int i = 0; i < frames(0.1); ++i) e.next();
    CHECK(e.stage() == AdsrEnvelope::Stage::Sustain);
    CHECK_NEAR(e.level(), 0.5, 1e-6);

    e.gateOff();
    for (int i = 0; i < frames(0.2); ++i) e.next();
    CHECK_NEAR(e.level(), 0.005, 0.001);
    for (int i = 0; i < frames(0.4); ++i) e.next();
    CHECK(e.idle());
}

TEST(synth, envelope_with_no_sustain_ends_while_the_key_is_held)
{
    AdsrEnvelope e;
    e.configure({0.001, 0.05, 0.0, 1.0}, rate);
    e.gateOn();
    for (int i = 0; i < frames(0.2); ++i) e.next();
    CHECK(e.idle());
}

TEST(synth, retriggered_envelope_attacks_from_where_it_is)
{
    AdsrEnvelope e;
    e.configure({0.01, 0.1, 0.5, 0.5}, rate);
    e.gateOn();
    for (int i = 0; i < frames(0.3); ++i) e.next();
    e.gateOff();
    for (int i = 0; i < 100; ++i) e.next();
    const float before = e.level();
    e.gateOn();
    CHECK(e.next() >= before);                           // no drop to 0: no click
}

// ---------------------------------------------------------------- LFO

TEST(synth, lfo_rate_follows_the_tempo_when_synced)
{
    Lfo l;
    l.rateHz = 3.0;
    CHECK_NEAR(lfoRateHz(l, 120.0), 3.0, 1e-12);
    l.sync = true;
    l.division = 2;                                      // 1/4 at 120 BPM: twice a second
    CHECK_NEAR(lfoRateHz(l, 120.0), 2.0, 1e-12);
    l.division = 3;                                      // 1/8
    CHECK_NEAR(lfoRateHz(l, 90.0), 3.0, 1e-12);
    l.division = 6;                                      // eighth triplet: three per beat
    CHECK_NEAR(lfoRateHz(l, 60.0), 3.0, 1e-12);
}

TEST(synth, synced_lfo_locks_to_the_song_beat)
{
    Settings s = plain();
    s.lfo.sync = true;
    s.lfo.division = 3;                                  // 1/8: two cycles per beat
    Run run(s);
    run.playing = true;
    run.beat = 10.125;                                   // a quarter of an eighth into the beat
    run.render(32);
    CHECK_NEAR(run.synth.lfoPhase(), 0.25, 1e-9);

    // Free rate when the song is stopped: it runs on from there.
    run.playing = false;
    s.lfo.sync = false;
    s.lfo.rateHz = 1.0;
    run.synth.setSettings(s);
    run.render(frames(0.5));
    CHECK_NEAR(run.synth.lfoPhase(), 0.75, 0.002);
}

TEST(synth, lfo_vibrato_moves_the_pitch_both_ways)
{
    Settings s = plain();
    s.lfo.target = LfoTarget::Pitch;
    s.lfo.shape = LfoShape::Square;
    s.lfo.rateHz = 0.5;                                  // one second up, one second down
    s.lfo.depth = std::sqrt(1.0 / 12.0);                 // one semitone
    Run run(s);
    run.on(69);
    const auto v = run.render(frames(2.0));
    CHECK_NEAR(peakHz(spectrumDb(v, std::size_t(frames(0.2)))), keyHz(70), 3.0);
    CHECK_NEAR(peakHz(spectrumDb(v, std::size_t(frames(1.2)))), keyHz(68), 3.0);
}

// ---------------------------------------------------------------- the instrument

TEST(synth, a_key_plays_at_its_pitch_and_stops_after_release)
{
    Run run;
    run.on(69);
    const auto held = run.render(frames(0.5));
    CHECK_NEAR(peakHz(spectrumDb(held, std::size_t(frames(0.1)))), 440.0, 3.0);
    CHECK(rms(held, std::size_t(frames(0.1))) > 0.05);
    CHECK_NEAR(run.synth.activeVoices(), 1, 0);

    run.off(69);
    const auto after = run.render(frames(0.5));
    CHECK(rms(after, std::size_t(frames(0.3))) < 1e-4);
    CHECK_NEAR(run.synth.activeVoices(), 0, 0);
}

TEST(synth, oscillator_tuning_moves_the_pitch)
{
    Settings s = plain();
    s.osc[0].octave = 1;
    s.osc[0].semitones = -5;                             // up a fourth from A4: E5
    Run run(s);
    run.on(69);
    const auto v = run.render(frames(0.5));
    CHECK_NEAR(peakHz(spectrumDb(v, std::size_t(frames(0.1)))), keyHz(76), 3.0);
}

TEST(synth, the_filter_envelope_opens_and_closes_the_sound)
{
    Settings s;
    s.osc[1].on = false;
    s.filter.cutoffHz = 200.0;
    s.filter.keyTrack = 0.0;
    s.filter.resonance = 0.0;
    s.filter.envOctaves = 5.0;
    s.filter.velocity = 0.0;
    s.filterEnv = {0.001, 3.0, 0.0, 0.1};
    s.ampEnv = {0.001, 0.001, 1.0, 0.05};
    Run run(s);
    run.on(57);
    const auto v = run.render(frames(4.0));
    // Bright at first (above 2 kHz through the first window), dark after the decay (200 Hz):
    // the 8th harmonic, 1.76 kHz, falls away.
    const auto early = spectrumDb(v);
    const auto late = spectrumDb(v, std::size_t(frames(4.0)) - fftSize);
    const double h8 = keyHz(57) * 8.0;
    const int bin = int(std::lround(h8 * fftSize / rate));
    CHECK(early[std::size_t(bin)] > late[std::size_t(bin)] + 30.0);
}

TEST(synth, polyphony_lets_the_oldest_key_go)
{
    Settings s = plain();
    s.ampEnv.release = 2.0;                              // long tails: only the steal frees voices
    Run run(s);
    for (int k = 0; k < Synth::Polyphony + 4; ++k) run.on(40 + k);
    run.render(frames(0.1));
    CHECK(run.synth.activeVoices() <= Synth::Polyphony);
    CHECK(run.synth.activeVoices() >= Synth::Polyphony - 1);
}

TEST(synth, the_same_key_again_reuses_its_voice)
{
    Settings s = plain();
    s.ampEnv.release = 2.0;
    Run run(s);
    for (int i = 0; i < 5; ++i) { run.on(60); run.render(256); run.off(60); run.render(256); }
    CHECK_NEAR(run.synth.activeVoices(), 1, 0);
}

TEST(synth, legato_goes_back_to_the_key_still_held)
{
    Settings s = plain();
    s.mode = PlayMode::Legato;
    Run run(s);
    run.on(69);
    run.render(frames(0.1));
    run.on(76);
    const auto top = run.render(frames(0.5));
    CHECK_NEAR(run.synth.activeVoices(), 1, 0);
    CHECK_NEAR(peakHz(spectrumDb(top, std::size_t(frames(0.1)))), keyHz(76), 3.0);

    run.off(76);                                         // 69 is still down
    const auto back = run.render(frames(0.5));
    CHECK_NEAR(peakHz(spectrumDb(back, std::size_t(frames(0.1)))), 440.0, 3.0);
    CHECK(rms(back, std::size_t(frames(0.1))) > 0.05);

    run.off(69);
    run.render(frames(0.3));
    CHECK_NEAR(run.synth.activeVoices(), 0, 0);
}

TEST(synth, glide_slides_between_keys_in_mono)
{
    Settings s = plain();
    s.mode = PlayMode::Mono;
    s.glideSeconds = 1.0;
    Run run(s);
    run.on(57);                                          // 220 Hz
    run.render(frames(0.2));
    run.on(69);                                          // to 440 Hz over a second
    const auto v = run.render(frames(2.0));
    // Counting upward zero crossings over 20 ms windows.
    const auto freqAt = [&](double seconds) {
        const auto from = std::size_t(frames(seconds)), to = from + std::size_t(frames(0.02));
        int crossings = 0;
        for (std::size_t i = from + 1; i < to; ++i) crossings += v[i - 1] <= 0.0f && v[i] > 0.0f;
        return crossings / 0.02;
    };
    const double early = freqAt(0.05), late = freqAt(1.8);
    CHECK(early > 230.0 && early < 400.0);
    CHECK_NEAR(late, 440.0, 60.0);
}

TEST(synth, pitch_bend_follows_the_bend_range)
{
    Settings s = plain();
    s.bendSemitones = 7;
    Run run(s);
    run.event(0xE0, 0x7F, 0x7F);                         // full up
    run.on(69);
    const auto v = run.render(frames(0.5));
    CHECK_NEAR(peakHz(spectrumDb(v, std::size_t(frames(0.1)))), keyHz(69 + 7.0 * 8191.0 / 8192.0), 3.0);
}

TEST(synth, sustain_pedal_holds_released_keys)
{
    Run run;
    run.event(0xB0, 64, 127);
    run.on(60);
    run.render(256);
    run.off(60);
    run.render(frames(0.5));
    CHECK_NEAR(run.synth.activeVoices(), 1, 0);
    run.event(0xB0, 64, 0);
    run.render(frames(0.5));
    CHECK_NEAR(run.synth.activeVoices(), 0, 0);
}

TEST(synth, all_notes_off_releases_everything)
{
    Run run;
    for (int k = 60; k < 64; ++k) run.on(k);
    run.render(256);
    run.event(0xB0, 123, 0);
    run.render(frames(0.5));
    CHECK_NEAR(run.synth.activeVoices(), 0, 0);
}

TEST(synth, loud_settings_stay_finite_and_bounded)
{
    Settings s;
    s.osc[1].on = true;
    s.osc[0].level = s.osc[1].level = 1.0;
    s.subLevel = s.noiseLevel = 1.0;
    s.filter.resonance = 1.0;
    s.filter.drive = 1.0;
    s.lfo.target = LfoTarget::Filter;
    s.lfo.depth = 1.0;
    s.lfo.rateHz = 7.0;
    Run run(s);
    for (int k = 36; k < 36 + Synth::Polyphony; k += 3) run.on(k, 127);
    for (const float x : run.render(frames(1.0))) {
        CHECK(std::isfinite(x));
        if (!(std::abs(x) < 8.0f)) { CHECK(std::abs(x) < 8.0f); break; }
    }
}

// ---------------------------------------------------------------- settings

TEST(synth, settings_round_trip_through_the_instrument)
{
    Settings s;                                          // values a float holds exactly
    s.osc[0] = {true, Wave::Pulse, -1, 7, 12.5, 0.25, 0.5};
    s.osc[1] = {true, Wave::Triangle, 2, -3, -25.0, 0.75, 0.25};
    s.subWave = SubWave::Square;
    s.subLevel = 0.5;
    s.noiseLevel = 0.125;
    s.filter = {FilterMode::BandPass, 1500.0, 0.75, 0.25, 1.0, -3.0, 0.5};
    s.filterEnv = {0.25, 0.5, 0.125, 1.5};
    s.ampEnv = {0.0625, 1.0, 0.75, 2.0};
    s.ampVelocity = 0.25;
    s.lfo = {LfoShape::Random, LfoTarget::PulseWidth, 2.5, true, 5, 0.5, 1.25};
    s.mode = PlayMode::Legato;
    s.glideSeconds = 0.25;
    s.bendSemitones = 12;
    s.volumeDb = -6.0;
    Synth synth;
    synth.setSettings(s);
    CHECK(synth.settings() == s);
    // Defaults like 0.3 do not survive a float exactly; the new instrument is near them.
    CHECK_NEAR(Synth().settings().filter.cutoffHz, Settings{}.filter.cutoffHz, 1e-3);
    CHECK(Synth().settings().mode == Settings{}.mode);
}

TEST(synth, sanitize_puts_everything_in_range)
{
    Settings s;
    s.osc[0].octave = 9;
    s.osc[0].fineCents = std::numeric_limits<double>::quiet_NaN();
    s.osc[1].wave = Wave(42);
    s.filter.cutoffHz = 1e9;
    s.filter.mode = FilterMode(-1);
    s.ampEnv.attack = 0.0;
    s.lfo.division = 99;
    s.lfo.rateHz = std::numeric_limits<double>::infinity();
    s.bendSemitones = -3;
    const auto c = sanitize(s);
    CHECK(c.osc[0].octave == MaxOctave);
    CHECK(c.osc[0].fineCents == 0.0);
    CHECK(c.osc[1].wave == Wave::Saw);
    CHECK(c.filter.cutoffHz == MaxCutoffHz);
    CHECK(c.filter.mode == FilterMode::LowPass12);
    CHECK(c.ampEnv.attack == MinEnvSeconds);
    CHECK(c.lfo.division == DivisionCount - 1);
    CHECK(c.lfo.rateHz == Lfo{}.rateHz);
    CHECK(c.bendSemitones == 0);
    CHECK(sanitize(Settings{}) == Settings{});
}

TEST(synth, names_round_trip)
{
    for (int i = 0; i < 4; ++i) { Wave v{}; CHECK(waveFromName(waveName(Wave(i)), v) && v == Wave(i)); }
    for (int i = 0; i < 2; ++i) { SubWave v{}; CHECK(subWaveFromName(subWaveName(SubWave(i)), v) && v == SubWave(i)); }
    for (int i = 0; i < 4; ++i) { FilterMode v{}; CHECK(filterModeFromName(filterModeName(FilterMode(i)), v) && v == FilterMode(i)); }
    for (int i = 0; i < 5; ++i) { LfoShape v{}; CHECK(lfoShapeFromName(lfoShapeName(LfoShape(i)), v) && v == LfoShape(i)); }
    for (int i = 0; i < 4; ++i) { LfoTarget v{}; CHECK(lfoTargetFromName(lfoTargetName(LfoTarget(i)), v) && v == LfoTarget(i)); }
    for (int i = 0; i < 3; ++i) { PlayMode v{}; CHECK(playModeFromName(playModeName(PlayMode(i)), v) && v == PlayMode(i)); }
    Wave w{};
    CHECK(!waveFromName("square", w));
}

TEST(synth, synth_tracks_play_through_the_synth)
{
    const auto instrument = createInstrument("Synth");
    CHECK(instrument && instrument->typeId() == "synth");
}

TEST(synth, every_setting_has_one_control_key_that_reads_and_writes_it)
{
    std::vector<std::string_view> keys;
    for (const auto& c : controls()) {
        CHECK(std::find(keys.begin(), keys.end(), c.key) == keys.end());
        keys.push_back(c.key);
        CHECK(findControl(c.key) == &c);
        CHECK(c.min <= c.max);
        if (c.kind == ControlKind::Choice) CHECK(c.max == double(c.choices.size() - 1));
        // Each control moves only its own field: writing max (or min) changes the settings.
        Settings s;
        const double was = c.get(s);
        CHECK(setControl(s, c.key, was == c.max ? c.min : c.max));
        CHECK(!(s == Settings{}));
        CHECK(sanitize(s) == s);
    }
    CHECK(keys.size() == 44);
}

TEST(synth, set_control_clamps_rounds_and_refuses_what_it_cannot_take)
{
    Settings s;
    CHECK(setControl(s, "filter.cutoff", 1e9) && s.filter.cutoffHz == MaxCutoffHz);
    CHECK(setControl(s, "osc2.octave", -2.6) && s.osc[1].octave == -3);
    CHECK(setControl(s, "lfo.sync", 0.9) && s.lfo.sync);
    CHECK(setControl(s, "lfo.division", 3.2) && s.lfo.division == 3);
    CHECK(!setControl(s, "filter.cutoff", std::numeric_limits<double>::quiet_NaN()));
    CHECK(!setControl(s, "nothing", 1.0));
    CHECK(setChoice(s, "osc1.wave", "triangle") && s.osc[0].wave == Wave::Triangle);
    CHECK(setChoice(s, "lfo.division", "6") && s.lfo.division == 6);
    CHECK(!setChoice(s, "osc1.wave", "square"));
    CHECK(!setChoice(s, "filter.cutoff", "saw"));            // not a choice
    CHECK(findControl("filter.mode")->choices[1] == "lowpass24");
}
