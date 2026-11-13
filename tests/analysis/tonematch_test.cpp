#include "testkit.h"
#include "analysis/dryguitar.h"
#include "analysis/tonefeatures.h"
#include "analysis/tonematch.h"
#include "instruments/guitar/plugins/guitarsuite/pedalregistry.h"
#include "processor/processor.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

using namespace analysis;

namespace {

constexpr double rate = 48000.0;

using Values = std::vector<std::pair<std::string, double>>;

// `sound` through pedals, as GuitarSuite would play it.
std::vector<float> through(std::vector<float> sound, const std::vector<std::pair<std::string, Values>>& board)
{
    std::vector<float> right = sound;
    for (const auto& [type, values] : board) {
        auto pedal = PedalRegistry::create(type);
        CHECK(pedal != nullptr);
        if (!pedal) return {};
        for (const auto& [id, value] : values)
            for (const auto& p : pedal->parameters())
                if (p->id == id) p->set(float(value));
        pedal->prepare(rate, 128);
        pedal->reset();
        for (std::size_t done = 0; done < sound.size(); done += 128) {
            float* channels[2] = {sound.data() + done, right.data() + done};
            ProcessContext context;
            context.audio = AudioView{channels, 2, int(std::min<std::size_t>(128, sound.size() - done))};
            context.sampleRate = rate;
            pedal->process(context);
        }
    }
    return sound;
}

std::vector<float> tone(double hz, double seconds, bool square)
{
    std::vector<float> out(std::size_t(seconds * rate));
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double v = std::sin(2.0 * std::numbers::pi * hz * double(i) / rate);
        out[i] = 0.3f * float(square ? (v >= 0.0 ? 1.0 : -1.0) : v);
    }
    return out;
}

const PedalPresetData* pedal(const ToneMatch& m, const std::string& type)
{
    for (const auto& p : m.preset.pedals)
        if (p.type == type) return &p;
    return nullptr;
}

} // namespace

TEST(tonematch, tone_features_tell_colour_squash_chords_and_tail)
{
    // Colour: a low tone and a high tone differ where they should; loudness does not matter.
    const auto low = measureTone(tone(200.0, 3.0, false), rate), high = measureTone(tone(3200.0, 3.0, false), rate);
    CHECK(low.valid && high.valid);
    int lowBand = 0, highBand = 0;
    for (int b = 0; b < ToneFeatures::Bands; ++b) {
        if (low.balanceDb[std::size_t(b)] > low.balanceDb[std::size_t(lowBand)]) lowBand = b;
        if (high.balanceDb[std::size_t(b)] > high.balanceDb[std::size_t(highBand)]) highBand = b;
    }
    CHECK(std::abs(ToneFeatures::bandHz(lowBand) - 200.0) < 15.0 && std::abs(ToneFeatures::bandHz(highBand) - 3200.0) < 250.0);
    CHECK(std::abs(ToneFeatures::bandHz(0) - 78.7) < 0.5 && std::abs(ToneFeatures::bandHz(ToneFeatures::Bands - 1) - 8000.0) < 1.0);
    auto quiet = dryGuitarChords(rate);
    const auto loud = measureTone(quiet, rate);
    for (float& v : quiet) v *= 0.1f;
    const auto quieter = measureTone(quiet, rate);
    CHECK(balanceDistance(loud, quieter) < 0.1 && std::abs(loud.levelDb - quieter.levelDb - 20.0) < 0.2);
    CHECK(std::abs(loud.crestDb - quieter.crestDb) < 0.1);
    CHECK(balanceDistance(low, high) > 20.0);
    // Squash: a sine's crest is 3 dB, a square's 0.
    const auto square = measureTone(tone(200.0, 3.0, true), rate);
    std::printf("    crest: sine %.1f dB, square %.1f dB\n", low.crestDb, square.crestDb);
    CHECK(std::abs(low.crestDb - 3.0) < 0.3 && square.crestDb < 0.6);
    // Chords against single notes, on the built-in dry guitar.
    const auto chords = measureTone(dryGuitarChords(rate), rate), solo = measureTone(dryGuitarSolo(rate), rate);
    std::printf("    dry guitar: chords %.2f, solo %.2f of the time in chords; level %.1f dBFS\n", chords.chordShare, solo.chordShare, chords.levelDb);
    CHECK(chords.chordShare > 0.5 && solo.chordShare < 0.2);
    // The tail: bursts that stop dead, and bursts that ring on.
    for (double ring : {0.02, 0.5}) {
        std::vector<float> sound(std::size_t(8.0 * rate), 0.0f);
        std::uint32_t seed = 5;
        for (int burst = 0; burst < 3; ++burst)
            for (std::size_t i = 0; i < std::size_t(2.4 * rate); ++i) {
                seed = seed * 1664525u + 1013904223u;
                const double t = double(i) / rate;
                const double level = t < 0.6 ? 1.0 : std::exp(-(t - 0.6) / ring * 6.9 * 0.5);   // 30 dB down after `ring`
                sound[std::size_t(burst * 2.5 * rate) + i] = float(0.3 * level * (double(seed >> 8) / double(1u << 24) * 2.0 - 1.0));
            }
        const auto t = measureTone(sound, rate);
        std::printf("    rings %.2f s: tail %.2f s\n", ring, t.tailSeconds);
        CHECK(t.tailSeconds >= 0.0 && std::abs(t.tailSeconds - ring) < 0.12);
    }
    CHECK(measureTone(tone(200.0, 3.0, false), rate).tailSeconds == -1.0);          // never stops
    CHECK(measureTone(tone(200.0, 3.0, false), rate, false).chordShare == 0.0);     // not asked for
    // Silence, and under a second.
    CHECK(!measureTone(std::vector<float>(std::size_t(3.0 * rate), 0.0f), rate).valid);
    CHECK(!measureTone(tone(200.0, 0.5, false), rate).valid);
}

TEST(tonematch, the_dry_guitar_is_the_same_every_time_at_a_plugged_in_level)
{
    const auto chords = dryGuitarChords(rate), again = dryGuitarChords(rate), solo = dryGuitarSolo(rate);
    CHECK(chords.size() == std::size_t(DryGuitarSeconds * rate) && solo.size() == chords.size());
    CHECK(chords == again && chords != solo);
    for (const auto* sound : {&chords, &solo}) {
        const auto t = measureTone(*sound, rate);
        CHECK(t.valid && t.levelDb > -34.0 && t.levelDb < -27.0);
        float peak = 0.0f;
        for (float v : *sound) peak = std::max(peak, std::abs(v));
        CHECK(peak < 0.5f);
    }
    CHECK(dryGuitarChords(44100.0).size() == std::size_t(DryGuitarSeconds * 44100.0));
    CHECK(dryGuitarChords(0.0).empty() && dryGuitarSolo(0.0).empty());
}

TEST(tonematch, a_high_gain_chord_tone_is_found_as_one_and_matched_closely)
{
    // The reference: the dry chords through a known high-gain board.
    const auto reference = through(dryGuitarChords(rate), {
        {"pedal.amp", {{"channel", 2}, {"drive", 26}, {"bass", 3}, {"mid", -3}, {"treble", 3}, {"presence", 0.5}}},
        {"pedal.cabinet", {{"model", 2}}},
    });
    int reports = 0;
    double last = 0.0;
    Progress progress;
    progress.report = [&](double v) { ++reports; CHECK(v >= last - 1e-9 && v <= 1.0); last = v; };
    const auto m = matchTone(reference, rate, {}, progress);
    CHECK(m.has_value());
    if (!m) return;
    std::printf("    found %s%s, %s; colour off by %.1f dB, crest %.1f against %.1f; %d renders\n", toneChannelName(m->channel),
                m->overdrive ? " + Overdrive" : "", toneCabinetName(m->cabinet), m->balanceErrorDb, m->matched.crestDb, m->reference.crestDb, m->renders);
    CHECK(!m->solo && m->channel >= 1);
    CHECK(m->balanceErrorDb < 2.5);
    CHECK(std::abs(m->matched.crestDb - m->reference.crestDb) < 1.5);
    CHECK(!m->reverb && !m->delay);
    CHECK(reports > 20 && std::abs(last - 1.0) < 1e-9);
    // The preset: a gate, the amp, the cabinet (a built-in model, no file), the EQ; every setting written.
    CHECK(m->preset.pedals.size() >= 4 && m->preset.pedals.front().type == "pedal.noisegate");
    const auto* amp = pedal(*m, "pedal.amp");
    const auto* cabinet = pedal(*m, "pedal.cabinet");
    CHECK(amp && cabinet && pedal(*m, "pedal.parametriceq"));
    if (!amp || !cabinet) return;
    CHECK(amp->parameters.size() == 8 && int(std::lround(amp->parameters.at("channel"))) == m->channel);
    CHECK(cabinet->irPath.empty() && int(std::lround(cabinet->parameters.at("model"))) == m->cabinet && m->cabinet >= 1);
    for (const auto& p : m->preset.pedals) CHECK(!p.bypassed && !p.parameters.empty());
    // It reads back as a preset.
    auto named = m->preset;
    named.name = "Found";
    const auto back = GuitarSuitePreset::parse(GuitarSuitePreset::serialize(named));
    CHECK(back.has_value() && back->pedals.size() == m->preset.pedals.size() && back->name == "Found");
}

TEST(tonematch, a_clean_solo_with_reverb_is_found_as_single_notes_with_a_room)
{
    const auto reference = through(dryGuitarSolo(rate), {
        {"pedal.amp", {{"channel", 0}, {"drive", 6}}},
        {"pedal.cabinet", {{"model", 3}}},
        {"pedal.reverb", {{"room", 0.85}, {"mix", 0.45}}},
    });
    const auto m = matchTone(reference, rate);
    CHECK(m.has_value());
    if (!m) return;
    std::printf("    found %s, %s, tail %.2f s, %s; colour off by %.1f dB\n", m->solo ? "single notes" : "chords", toneChannelName(m->channel),
                m->reference.tailSeconds, m->reverb ? "reverb" : "dry", m->balanceErrorDb);
    CHECK(m->solo && m->reverb && pedal(*m, "pedal.reverb"));
    CHECK(m->balanceErrorDb < 3.5);
    // Asked for chords instead: it does as asked.
    ToneMatchOptions chords;
    chords.playing = TonePlaying::Chords;
    std::atomic<bool> cancel{false};
    Progress progress;
    progress.cancel = &cancel;
    int reports = 0;
    progress.report = [&](double) { if (++reports == 5) cancel = true; };   // and cancelled part-way: nothing
    CHECK(!matchTone(reference, rate, chords, progress).has_value());
    CHECK(!matchTone(std::vector<float>(std::size_t(1.0 * rate), 0.1f), rate).has_value());       // too short
    CHECK(!matchTone(std::vector<float>(std::size_t(4.0 * rate), 0.0f), rate).has_value());       // silence
    CHECK(std::string(toneChannelName(0)) == "Clean" && std::string(toneChannelName(2)) == "High Gain");
    CHECK(std::string(toneCabinetName(1)) == "1x12 Open" && std::string(toneCabinetName(3)) == "4x12 Closed");
}
