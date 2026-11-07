#include "testkit.h"
#include "instruments/sampler/sampler.h"
#include "instruments/sampler/samplervoice.h"
#include "instruments/sampler/zonemap.h"

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <thread>

namespace {

std::shared_ptr<const AudioData> shortSample()
{
    auto audio = std::make_shared<AudioData>();
    audio->channels = {std::vector<float>(64, 0.5f)};
    audio->frames = 64;
    return audio;
}

SampleZone zone(int loKey, int hiKey, int loVel = 0, int hiVel = 127)
{
    SampleZone z;
    z.audio = shortSample();
    z.loKey = loKey; z.hiKey = hiKey;
    z.loVelocity = loVel; z.hiVelocity = hiVel;
    return z;
}

int findOne(const ZoneMap& map, int key, int velocity, std::uint32_t rr = 0,
            ZoneMap::Trigger trigger = ZoneMap::Trigger::Attack)
{
    std::array<int, 8> out{};
    return map.find(key, velocity, trigger, rr, out) == 1 ? out[0] : -1;
}

} // namespace

TEST(sampler, zonemap_picks_zone_by_key_range)
{
    ZoneMap map;
    CHECK(map.setZones({zone(0, 59), zone(60, 127)}) == 0);
    CHECK(findOne(map, 0, 100) == 0);
    CHECK(findOne(map, 59, 100) == 0);
    CHECK(findOne(map, 60, 100) == 1);
    CHECK(findOne(map, 127, 100) == 1);
    std::array<int, 4> out{};
    CHECK(map.find(-1, 100, ZoneMap::Trigger::Attack, 0, out) == 0);
    CHECK(map.find(128, 100, ZoneMap::Trigger::Attack, 0, out) == 0);
}

TEST(sampler, zonemap_picks_velocity_layer_with_inclusive_edges)
{
    ZoneMap map;
    map.setZones({zone(36, 36, 1, 63), zone(36, 36, 64, 127)});
    CHECK(findOne(map, 36, 1) == 0);
    CHECK(findOne(map, 36, 63) == 0);
    CHECK(findOne(map, 36, 64) == 1);
    CHECK(findOne(map, 36, 127) == 1);
    std::array<int, 4> out{};
    CHECK(map.find(36, 0, ZoneMap::Trigger::Attack, 0, out) == 0);   // below every layer
}

TEST(sampler, zonemap_cycles_round_robins)
{
    std::vector<SampleZone> zones;
    for (int position = 1; position <= 3; ++position) {
        auto z = zone(38, 38);
        z.roundRobinLength = 3; z.roundRobinPosition = position;
        zones.push_back(z);
    }
    ZoneMap map;
    map.setZones(zones);
    CHECK(findOne(map, 38, 100, 0) == 0);
    CHECK(findOne(map, 38, 100, 1) == 1);
    CHECK(findOne(map, 38, 100, 2) == 2);
    CHECK(findOne(map, 38, 100, 3) == 0);
    CHECK(findOne(map, 38, 100, 0xFFFFFFFFu) == int(0xFFFFFFFFu % 3));
}

TEST(sampler, zonemap_separates_attack_and_release_zones)
{
    auto release = zone(60, 60);
    release.releaseTrigger = true;
    ZoneMap map;
    map.setZones({zone(60, 60), release});
    CHECK(findOne(map, 60, 100, 0, ZoneMap::Trigger::Attack) == 0);
    CHECK(findOne(map, 60, 100, 0, ZoneMap::Trigger::Release) == 1);
}

TEST(sampler, zonemap_returns_layered_zones_in_order_and_respects_buffer_size)
{
    ZoneMap map;
    map.setZones({zone(40, 40), zone(30, 50), zone(40, 40)});    // e.g. close, room, overhead mics
    std::array<int, 8> out{};
    CHECK(map.find(40, 100, ZoneMap::Trigger::Attack, 0, out) == 3);
    CHECK(out[0] == 0 && out[1] == 1 && out[2] == 2);
    std::array<int, 2> small{};
    CHECK(map.find(40, 100, ZoneMap::Trigger::Attack, 0, small) == 2);
    CHECK(small[0] == 0 && small[1] == 1);
    CHECK(map.find(40, 100, ZoneMap::Trigger::Attack, 0, std::span<int>{}) == 0);
}

TEST(sampler, zonemap_drops_zones_that_can_never_play)
{
    auto noAudio = zone(60, 60);   noAudio.audio = nullptr;
    auto empty = zone(60, 60);     empty.audio = std::make_shared<AudioData>();
    auto keys = zone(70, 60);
    auto vel = zone(60, 60, 90, 80);
    auto badKey = zone(0, 128);
    auto rr = zone(60, 60);        rr.roundRobinLength = 2; rr.roundRobinPosition = 3;
    auto rrZero = zone(60, 60);    rrZero.roundRobinLength = 0;
    ZoneMap map;
    CHECK(map.setZones({noAudio, empty, keys, zone(60, 60), vel, badKey, rr, rrZero}) == 7);
    CHECK(map.size() == 1);
    CHECK(findOne(map, 60, 100) == 0);

    CHECK(map.setZones({}) == 0);                                // replacing clears the old index
    std::array<int, 4> out{};
    CHECK(map.find(60, 100, ZoneMap::Trigger::Attack, 0, out) == 0);
}


// ---------------------------------------------------------------- SamplerVoice

namespace {

constexpr double rate = 48000.0;

std::shared_ptr<const AudioData> makeAudio(std::vector<std::vector<float>> channels, double sampleRate = rate)
{
    auto audio = std::make_shared<AudioData>();
    audio->frames = std::int64_t(channels.front().size());
    audio->channels = std::move(channels);
    audio->sampleRate = sampleRate;
    return audio;
}

std::vector<float> ramp(int frames)
{
    std::vector<float> v(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) v[std::size_t(i)] = float(i);
    return v;
}

SampleZone plainZone(std::shared_ptr<const AudioData> audio)
{
    SampleZone z;
    z.audio = std::move(audio);
    z.rootKey = 60;
    z.attack = 0.0f; z.decay = 0.0f; z.sustain = 1.0f; z.release = 0.0f;
    return z;
}

// A one-second constant 1.0 that loops, so only the envelope shapes the output.
SampleZone dcZone()
{
    auto z = plainZone(makeAudio({std::vector<float>(48000, 1.0f)}));
    z.looping = true; z.loopStart = 0; z.loopEnd = 47999;
    return z;
}

struct Out {
    std::vector<float> left, right;
    explicit Out(int frames) : left(std::size_t(frames), 0.0f), right(std::size_t(frames), 0.0f) {}
};

Out render(SamplerVoice& voice, int frames)
{
    Out out(frames);
    voice.render(out.left.data(), out.right.data(), frames);
    return out;
}

} // namespace

TEST(sampler, voice_plays_sample_unchanged_at_root_key_and_full_velocity)
{
    const auto zone = plainZone(makeAudio({ramp(32)}));
    SamplerVoice voice;
    voice.start(zone, 60, 127, rate);
    CHECK(voice.active());
    const auto out = render(voice, 32);
    for (int i = 0; i < 32; ++i) {
        CHECK_NEAR(out.left[std::size_t(i)], float(i), 1e-4);       // frame 0 included: no lost transient
        CHECK_NEAR(out.right[std::size_t(i)], float(i), 1e-4);
    }
}

TEST(sampler, voice_stops_at_sample_end_and_adds_to_the_output)
{
    const auto zone = plainZone(makeAudio({std::vector<float>(10, 0.25f)}));
    SamplerVoice voice;
    voice.start(zone, 60, 127, rate);
    Out out(20);
    std::fill(out.left.begin(), out.left.end(), 1.0f);
    voice.render(out.left.data(), out.right.data(), 20);
    CHECK(!voice.active());
    CHECK_NEAR(out.left[0], 1.25, 1e-6);                           // mixed in, not overwritten
    CHECK_NEAR(out.left[15], 1.0, 1e-6);
    CHECK_NEAR(out.right[15], 0.0, 1e-6);
    Out after(4);
    voice.render(after.left.data(), after.right.data(), 4);       // a stopped voice adds nothing
    CHECK(after.left[0] == 0.0f);
}

TEST(sampler, voice_pitch_follows_key_tune_and_sample_rate)
{
    const auto audio = makeAudio({ramp(100)});
    auto zone = plainZone(audio);
    SamplerVoice voice;
    voice.start(zone, 72, 127, rate);
    CHECK_NEAR(voice.increment(), 2.0, 1e-12);
    voice.start(zone, 48, 127, rate);
    CHECK_NEAR(voice.increment(), 0.5, 1e-12);
    zone.tuneCents = 100.0f;
    voice.start(zone, 60, 127, rate);
    CHECK_NEAR(voice.increment(), std::exp2(1.0 / 12.0), 1e-6);

    const auto zone44 = plainZone(makeAudio({ramp(100)}, 44100.0));
    voice.start(zone44, 60, 127, rate);
    CHECK_NEAR(voice.increment(), 44100.0 / 48000.0, 1e-12);

    const auto octave = plainZone(audio);                           // the zone must outlive the note
    voice.start(octave, 72, 127, rate);                             // an octave up skips every other frame
    const auto out = render(voice, 10);
    for (int i = 0; i < 10; ++i) CHECK_NEAR(out.left[std::size_t(i)], 2.0 * i, 1e-4);
}

TEST(sampler, voice_hermite_interpolation_keeps_a_sine_clean)
{
    constexpr double f = 1000.0;
    std::vector<float> sine(4800);
    for (std::size_t n = 0; n < sine.size(); ++n) sine[n] = float(std::sin(2.0 * std::numbers::pi * f * double(n) / rate));
    auto zone = plainZone(makeAudio({sine}));
    zone.tuneCents = -700.0f;                                       // a fifth down: never on a frame
    SamplerVoice voice;
    voice.start(zone, 60, 127, rate);
    const double ratio = std::exp2(-7.0 / 12.0);
    const auto out = render(voice, 4000);
    double worst = 0.0;
    for (int n = 2; n < 4000; ++n) {
        const double expected = std::sin(2.0 * std::numbers::pi * f * ratio * n / rate);
        worst = std::max(worst, std::abs(out.left[std::size_t(n)] - expected));
    }
    CHECK(worst < 1e-3);                                            // linear would be ~2e-3
}

TEST(sampler, voice_velocity_volume_and_pan_set_the_gain)
{
    const auto mono = makeAudio({std::vector<float>(8, 1.0f)});
    auto zone = plainZone(mono);
    SamplerVoice voice;
    voice.start(zone, 60, 64, rate);
    auto out = render(voice, 1);
    CHECK_NEAR(out.left[0], (64.0 / 127.0) * (64.0 / 127.0), 1e-6);

    zone.volumeDb = -6.0f;
    voice.start(zone, 60, 127, rate);
    out = render(voice, 1);
    CHECK_NEAR(out.left[0], std::pow(10.0, -6.0 / 20.0), 1e-6);

    zone.volumeDb = 0.0f;
    zone.pan = -1.0f;                                               // mono, hard left: equal power
    voice.start(zone, 60, 127, rate);
    out = render(voice, 1);
    CHECK_NEAR(out.left[0], std::sqrt(2.0), 1e-5);
    CHECK_NEAR(out.right[0], 0.0, 1e-5);
    zone.pan = 0.5f;
    voice.start(zone, 60, 127, rate);
    out = render(voice, 1);
    CHECK_NEAR(out.left[0] * out.left[0] + out.right[0] * out.right[0], 2.0, 1e-5);

    auto stereo = plainZone(makeAudio({std::vector<float>(8, 1.0f), std::vector<float>(8, 0.5f)}));
    voice.start(stereo, 60, 127, rate);                             // stereo, centre: both untouched
    out = render(voice, 1);
    CHECK_NEAR(out.left[0], 1.0, 1e-6);
    CHECK_NEAR(out.right[0], 0.5, 1e-6);
    stereo.pan = 1.0f;                                              // stereo, hard right: balance
    voice.start(stereo, 60, 127, rate);
    out = render(voice, 1);
    CHECK_NEAR(out.left[0], 0.0, 1e-6);
    CHECK_NEAR(out.right[0], 0.5, 1e-6);
}

TEST(sampler, voice_loops_between_inclusive_loop_points)
{
    auto zone = plainZone(makeAudio({ramp(100)}));
    zone.looping = true; zone.loopStart = 20; zone.loopEnd = 39;
    SamplerVoice voice;
    voice.start(zone, 60, 127, rate);
    const auto out = render(voice, 1000);
    CHECK(voice.active());
    for (int i = 0; i < 40; ++i) CHECK_NEAR(out.left[std::size_t(i)], float(i), 1e-4);
    for (int i = 40; i < 1000; ++i) CHECK_NEAR(out.left[std::size_t(i)], float(20 + (i - 20) % 20), 1e-4);

    zone.tuneCents = 3600.0f;                                       // 3 octaves up: 8 frames a step
    zone.loopStart = 0; zone.loopEnd = 3;                           // on a 4-frame loop, so every step wraps twice
    voice.start(zone, 60, 127, rate);
    render(voice, 100);
    CHECK(voice.active());
    CHECK(voice.position() >= 0.0 && voice.position() < 4.0);

    zone.loopEnd = 100;                                             // past the sample: plays once
    zone.tuneCents = 0.0f;
    voice.start(zone, 60, 127, rate);
    render(voice, 200);
    CHECK(!voice.active());
}

TEST(sampler, voice_attack_rises_linearly)
{
    auto zone = dcZone();
    zone.attack = 0.01f;                                            // 480 frames
    SamplerVoice voice;
    voice.start(zone, 60, 127, rate);
    const auto out = render(voice, 1000);
    CHECK_NEAR(out.left[0], 1.0 / 480.0, 1e-5);
    CHECK_NEAR(out.left[239], 0.5, 1e-3);
    CHECK_NEAR(out.left[479], 1.0, 1e-3);
    CHECK_NEAR(out.left[999], 1.0, 1e-6);
    CHECK(voice.stage() == SamplerVoice::Stage::Sustain);
}

TEST(sampler, voice_decays_to_sustain_or_ends_when_sustain_is_zero)
{
    auto zone = dcZone();
    zone.decay = 0.1f; zone.sustain = 0.5f;                         // -60 dB of the way in 4800 frames
    SamplerVoice voice;
    voice.start(zone, 60, 127, rate);
    auto out = render(voice, 6000);
    CHECK(voice.stage() == SamplerVoice::Stage::Sustain);
    CHECK_NEAR(out.left[5999], 0.5, 1e-6);
    CHECK(out.left[100] < out.left[1] && out.left[100] > 0.5f);

    zone.sustain = 0.0f;                                            // a one-shot that dies away
    voice.start(zone, 60, 127, rate);
    out = render(voice, 6000);
    CHECK(!voice.active());
    CHECK(out.left[5999] == 0.0f);
}

TEST(sampler, voice_release_falls_60_db_in_release_time_then_stops)
{
    auto zone = dcZone();
    zone.release = 0.1f;                                            // 4800 frames
    SamplerVoice voice;
    voice.start(zone, 60, 127, rate);
    render(voice, 100);
    voice.release();
    CHECK(voice.releasing());
    const auto out = render(voice, 4790);
    CHECK(voice.active());
    CHECK_NEAR(20.0 * std::log10(out.left[2399]), -30.0, 0.1);     // halfway in time = -30 dB
    render(voice, 20);
    CHECK(!voice.active());

    zone.release = 0.0f;                                            // no release time: silent next frame
    voice.start(zone, 60, 127, rate);
    render(voice, 10);
    voice.release();
    const auto cut = render(voice, 2);
    CHECK(!voice.active());
    CHECK(cut.left[0] == 0.0f);

    voice.start(zone, 60, 127, rate);
    voice.stop();
    CHECK(!voice.active() && voice.zone() == nullptr);
}


// ---------------------------------------------------------------- Sampler

namespace {

// A held, looping zone on one key; `value` tells the zones apart in the output.
SampleZone keyZone(int key, float value = 1.0f)
{
    auto z = plainZone(makeAudio({std::vector<float>(4800, value)}));
    z.loKey = z.hiKey = key;
    z.looping = true; z.loopStart = 0; z.loopEnd = 4799;
    z.release = 0.05f;
    return z;
}

int countVoices(const Sampler& sampler, int key, bool wantFading = false)
{
    int n = 0;
    for (const auto& v : sampler.voices())
        if (v.active() && v.key() == key && v.fading() == wantFading) ++n;
    return n;
}

const SamplerVoice* voiceOn(const Sampler& sampler, int key)
{
    for (const auto& v : sampler.voices())
        if (v.active() && v.key() == key) return &v;
    return nullptr;
}

} // namespace

TEST(sampler, sampler_note_on_plays_and_note_off_releases)
{
    Sampler sampler;
    sampler.setPolyphony(8);
    CHECK(sampler.setZones({keyZone(60), keyZone(61)}) == 0);
    sampler.noteOn(60, 127);
    CHECK(sampler.activeVoices() == 1);
    CHECK(voiceOn(sampler, 60) && voiceOn(sampler, 60)->zone() == &sampler.zones().zone(0));
    Out out(4);
    sampler.render(out.left.data(), out.right.data(), 4);
    CHECK_NEAR(out.left[0], 1.0, 1e-6);

    sampler.noteOff(60);
    CHECK(voiceOn(sampler, 60)->releasing() && !voiceOn(sampler, 60)->fading());
    sampler.noteOn(61, 127);
    sampler.noteOn(61, 0);                                         // velocity 0 is a note-off
    CHECK(voiceOn(sampler, 61)->releasing());

    Out longer(4800);
    sampler.render(longer.left.data(), longer.right.data(), 4800);
    CHECK(sampler.activeVoices() == 0);

    sampler.noteOn(-1, 100);                                       // outside MIDI: ignored
    sampler.noteOn(128, 100);
    sampler.noteOn(62, 100);                                       // no zone on the key
    CHECK(sampler.activeVoices() == 0);
}

TEST(sampler, sampler_round_robins_count_per_key)
{
    std::vector<SampleZone> zones;
    for (int position = 1; position <= 3; ++position) {
        auto z = keyZone(38, float(position));
        z.roundRobinLength = 3; z.roundRobinPosition = position;
        zones.push_back(z);
    }
    zones.push_back(keyZone(42));
    Sampler sampler;
    sampler.setPolyphony(16);
    sampler.setZones(zones);
    const int expected[] = {0, 1, 2, 0};
    for (const int zone : expected) {
        sampler.noteOn(42, 100);                                   // another key does not advance 38
        sampler.noteOn(38, 100);
        const SamplerVoice* newest = nullptr;
        for (const auto& v : sampler.voices())
            if (v.active() && v.key() == 38 && !v.releasing()) newest = &v;
        CHECK(newest && newest->zone() == &sampler.zones().zone(zone));
        sampler.noteOff(38);
    }
}

TEST(sampler, sampler_release_zones_play_on_note_off_at_note_on_velocity)
{
    auto attack = keyZone(60);
    auto release = plainZone(makeAudio({std::vector<float>(100, 1.0f)}));
    release.loKey = release.hiKey = 60;
    release.loVelocity = 1; release.hiVelocity = 63;               // only soft notes have this one
    release.releaseTrigger = true;
    Sampler sampler;
    sampler.setPolyphony(8);
    sampler.setZones({attack, release});

    sampler.noteOff(60);                                           // never pressed: nothing
    CHECK(sampler.activeVoices() == 0);

    sampler.noteOn(60, 40);
    CHECK(sampler.activeVoices() == 1);                            // the release zone waits
    sampler.noteOff(60);
    CHECK(sampler.activeVoices() == 2);
    const SamplerVoice* rel = nullptr;
    for (const auto& v : sampler.voices())
        if (v.active() && v.zone() == &sampler.zones().zone(1)) rel = &v;
    CHECK(rel && !rel->releasing());                               // plays its own sample out
    sampler.noteOff(60);                                           // a second note-off: nothing more
    CHECK(sampler.activeVoices() == 2);

    sampler.reset();
    sampler.noteOn(60, 100);                                       // loud note: no release layer
    sampler.noteOff(60);
    CHECK(sampler.activeVoices() == 1);
}

TEST(sampler, sampler_one_shot_zones_ignore_note_off)
{
    auto hit = plainZone(makeAudio({std::vector<float>(100, 1.0f)}));
    hit.loKey = hit.hiKey = hit.rootKey = 36;                     // at its own pitch: 100 frames long
    hit.oneShot = true;
    Sampler sampler;
    sampler.setPolyphony(8);
    sampler.setZones({hit});
    sampler.noteOn(36, 100);
    sampler.noteOff(36);
    CHECK(voiceOn(sampler, 36) && !voiceOn(sampler, 36)->releasing());
    Out out(200);
    sampler.render(out.left.data(), out.right.data(), 200);
    CHECK(sampler.activeVoices() == 0);                            // ends with its sample
    CHECK(out.left[99] > 0.0f && out.left[100] == 0.0f);
}

TEST(sampler, sampler_closed_hihat_chokes_open_hihat)
{
    auto open = keyZone(46);   open.group = 2; open.offBy = 1;
    auto closed = keyZone(42); closed.group = 1;
    auto pedal = keyZone(44);  pedal.group = 1;
    auto kick = keyZone(36);
    Sampler sampler;
    sampler.setPolyphony(16);
    sampler.setZones({open, closed, pedal, kick});

    sampler.noteOn(46, 100);
    sampler.noteOn(36, 100);
    sampler.noteOn(42, 100);
    CHECK(countVoices(sampler, 46, true) == 1);                   // open hat fading
    CHECK(countVoices(sampler, 36) == 1);                         // kick untouched
    CHECK(countVoices(sampler, 42) == 1);
    Out out(480);                                                  // 10 ms: the 5 ms fade is over
    sampler.render(out.left.data(), out.right.data(), 480);
    CHECK(countVoices(sampler, 46, true) == 0 && countVoices(sampler, 46) == 0);

    sampler.noteOn(46, 100);
    sampler.noteOn(44, 100);                                       // the pedal chokes it as well
    CHECK(countVoices(sampler, 46, true) == 1);
}

TEST(sampler, sampler_choke_spares_layers_of_the_same_note_but_cuts_the_last_hit)
{
    auto close = keyZone(46, 1.0f); close.group = 2; close.offBy = 2;   // self-choking, two mics
    auto room = keyZone(46, 0.5f);  room.group = 2;  room.offBy = 2;
    Sampler sampler;
    sampler.setPolyphony(16);
    sampler.setZones({close, room});
    sampler.noteOn(46, 100);
    CHECK(countVoices(sampler, 46) == 2);
    sampler.noteOn(46, 100);
    CHECK(countVoices(sampler, 46) == 2);
    CHECK(countVoices(sampler, 46, true) == 2);
}

TEST(sampler, sampler_polyphony_fades_oldest_preferring_released_notes)
{
    std::vector<SampleZone> zones;
    for (int key = 60; key < 70; ++key) zones.push_back(keyZone(key));
    Sampler sampler;
    sampler.setPolyphony(4);
    sampler.setZones(zones);
    CHECK(sampler.voices().size() == 8);

    for (int key = 60; key < 66; ++key) sampler.noteOn(key, 100);
    CHECK(countVoices(sampler, 60, true) == 1);
    CHECK(countVoices(sampler, 61, true) == 1);
    for (int key = 62; key < 66; ++key) CHECK(countVoices(sampler, key) == 1);

    sampler.reset();
    sampler.noteOn(60, 100);
    sampler.noteOn(61, 100);
    sampler.noteOn(62, 100);
    sampler.noteOn(63, 100);
    sampler.noteOff(62);                                           // released, though not the oldest
    sampler.noteOn(64, 100);
    CHECK(countVoices(sampler, 62, true) == 1);
    CHECK(countVoices(sampler, 60) == 1 && countVoices(sampler, 61) == 1);
}

TEST(sampler, sampler_full_pool_cuts_a_fading_voice_and_never_grows)
{
    std::vector<SampleZone> zones;
    for (int key = 60; key < 70; ++key) zones.push_back(keyZone(key));
    Sampler sampler;
    sampler.setPolyphony(1);
    sampler.setZones(zones);
    for (int key = 60; key < 70; ++key) {
        sampler.noteOn(key, 100);
        CHECK(sampler.activeVoices() <= 2);
        CHECK(countVoices(sampler, key) == 1);                     // the newest note always sounds
    }
}

TEST(sampler, sampler_mixes_voices_and_replacing_zones_stops_them)
{
    Sampler sampler;
    sampler.setPolyphony(8);
    sampler.setZones({keyZone(60, 0.25f), keyZone(64, 0.5f)});
    sampler.noteOn(60, 127);
    sampler.noteOn(64, 127);
    Out out(2);
    sampler.render(out.left.data(), out.right.data(), 2);
    CHECK_NEAR(out.left[1], 0.75, 1e-6);
    CHECK_NEAR(out.right[1], 0.75, 1e-6);

    sampler.setZones({keyZone(60)});
    CHECK(sampler.activeVoices() == 0);
    sampler.noteOff(64);                                           // its hold was cleared too
    CHECK(sampler.activeVoices() == 0);
}


// ---------------------------------------------------------------- Sampler as a Processor

namespace {

struct Block {
    std::vector<std::vector<float>> data;
    std::vector<float*> pointers;
    MidiBuffer midi;
    Block(int channels, int frames, float fill = 0.0f)
        : data(std::size_t(channels), std::vector<float>(std::size_t(frames), fill))
    {
        for (auto& c : data) pointers.push_back(c.data());
    }
    void event(int offset, std::uint8_t status, std::uint8_t d1, std::uint8_t d2) { midi.add({offset, status, d1, d2}); }
    void run(Processor& processor)
    {
        ProcessContext context;
        context.audio = {pointers.data(), int(pointers.size()), int(data.front().size())};
        context.midi = &midi;
        context.sampleRate = rate;
        processor.process(context);
        midi.clear();
    }
};

} // namespace

TEST(sampler, process_plays_notes_at_their_frame_in_the_block)
{
    Sampler sampler;
    sampler.prepare(rate, 64);
    auto zone = keyZone(60);
    zone.release = 0.0f;
    sampler.setZones({zone});
    Block block(2, 64);
    block.event(10, 0x90, 60, 127);
    block.event(20, 0x80, 60, 0);
    block.run(sampler);
    for (int i = 0; i < 64; ++i) {
        const float expected = (i >= 10 && i < 20) ? 1.0f : 0.0f;
        CHECK_NEAR(block.data[0][std::size_t(i)], expected, 1e-6);
        CHECK_NEAR(block.data[1][std::size_t(i)], expected, 1e-6);
    }
    CHECK(sampler.activeVoices() == 0);

    block.event(5, 0x90, 60, 127);
    block.event(30, 0x90, 60, 0);                                  // note-on with velocity 0
    block.run(sampler);
    CHECK_NEAR(block.data[0][29], 1.0, 1e-6);
    CHECK_NEAR(block.data[0][30], 0.0, 1e-6);
}

TEST(sampler, process_adds_to_the_buffer_and_mixes_down_to_mono)
{
    Sampler sampler;
    sampler.prepare(rate, 64);
    auto zone = keyZone(60);
    zone.pan = -1.0f;                                               // mono sample, hard left
    sampler.setZones({zone});

    Block stereo(2, 8, 0.25f);
    stereo.event(0, 0x90, 60, 127);
    stereo.run(sampler);
    CHECK_NEAR(stereo.data[0][3], 0.25 + std::sqrt(2.0), 1e-5);
    CHECK_NEAR(stereo.data[1][3], 0.25, 1e-5);

    Block mono(1, 8);
    mono.run(sampler);                                              // still held
    CHECK_NEAR(mono.data[0][3], std::sqrt(2.0) / 2.0, 1e-5);
}

TEST(sampler, process_handles_blocks_longer_than_prepared)
{
    Sampler sampler;
    sampler.prepare(rate, 16);
    sampler.setZones({keyZone(60)});
    Block block(2, 100);
    block.event(0, 0x90, 60, 127);
    block.event(70, 0x90, 60, 0);
    block.run(sampler);
    for (int i = 0; i < 70; ++i) CHECK_NEAR(block.data[0][std::size_t(i)], 1.0, 1e-6);
    CHECK(block.data[0][99] < block.data[0][69]);                  // releasing after frame 70
}

TEST(sampler, process_all_notes_off_releases_and_all_sound_off_silences)
{
    Sampler sampler;
    sampler.prepare(rate, 256);
    sampler.setZones({keyZone(60), keyZone(64)});
    Block block(2, 256);
    block.event(0, 0x90, 60, 127);
    block.event(0, 0x90, 64, 127);
    block.event(100, 0xB0, 123, 0);
    block.run(sampler);
    CHECK(sampler.activeVoices() == 2);
    for (const auto& v : sampler.voices()) if (v.active()) CHECK(v.releasing() && !v.fading());
    CHECK(block.data[0][255] > 0.0f);                              // a 50 ms release, still sounding

    Block next(2, 256);
    next.event(10, 0xB0, 120, 0);
    next.event(500, 0x90, 64, 127);                                // past the block: at its end
    next.run(sampler);
    CHECK(next.data[0][9] > 0.0f);
    CHECK_NEAR(next.data[0][10], 0.0, 1e-9);
    CHECK_NEAR(next.data[0][255], 0.0, 1e-9);
    CHECK(sampler.activeVoices() == 1);                            // the late note waits for the next block

    Block ignored(2, 16);
    ignored.event(0, 0xE0, 0, 64);                                 // pitch bend: not handled yet
    ignored.run(sampler);
    CHECK(sampler.activeVoices() == 1);
}

TEST(sampler, works_as_a_processor_and_prepare_stops_the_old_voices)
{
    std::shared_ptr<Processor> processor = std::make_shared<Sampler>();
    CHECK(processor->typeId() == "sampler");
    auto& sampler = static_cast<Sampler&>(*processor);
    sampler.setZones({keyZone(60)});
    Block block(2, 32);
    block.event(0, 0x90, 60, 127);
    block.run(*processor);
    CHECK(sampler.activeVoices() == 1);
    processor->prepare(44100.0, 512);
    CHECK(sampler.activeVoices() == 0);
    CHECK(sampler.zones().size() == 1);                            // the zones stay
    Block empty(2, 0);
    empty.run(*processor);                                         // nothing to do, no crash
}


// ---------------------------------------------------------------- velocity curves, offset, delay

TEST(sampler, voice_uses_the_zone_velocity_curve)
{
    auto curve = std::make_shared<SampleZone::VelocityCurve>();
    for (int v = 0; v < 128; ++v) (*curve)[std::size_t(v)] = v >= 31 ? 1.0f : float(v) / 31.0f;
    auto zone = plainZone(makeAudio({std::vector<float>(64, 1.0f)}));
    SamplerVoice voice;
    voice.start(zone, 60, 31, rate);
    CHECK_NEAR(render(voice, 4).left[2], (31.0 / 127.0) * (31.0 / 127.0), 1e-6);   // no curve: velocity squared
    zone.velocityCurve = curve;
    voice.start(zone, 60, 31, rate);
    CHECK_NEAR(render(voice, 4).left[2], 1.0, 1e-6);                                 // full at 31
    voice.start(zone, 60, 10, rate);
    CHECK_NEAR(render(voice, 4).left[2], 10.0 / 31.0, 1e-6);
}

TEST(sampler, voice_starts_at_the_offset_after_the_delay)
{
    auto zone = plainZone(makeAudio({ramp(32)}));
    zone.offset = 10;
    SamplerVoice voice;
    voice.start(zone, 60, 127, rate);
    CHECK_NEAR(render(voice, 1).left[0], 10.0, 1e-6);

    zone.offset = 0;
    zone.delaySeconds = float(5.0 / rate);
    voice.start(zone, 60, 127, rate);
    const auto out = render(voice, 8);
    for (int i = 0; i < 5; ++i) CHECK(out.left[std::size_t(i)] == 0.0f);
    CHECK_NEAR(out.left[5], 0.0, 1e-6);                          // the sample's frame 0
    CHECK_NEAR(out.left[6], 1.0, 1e-6);

    zone.offset = 99;                                            // past the end: nothing to play
    zone.delaySeconds = 0.0f;
    voice.start(zone, 60, 127, rate);
    render(voice, 4);
    CHECK(!voice.active());

    zone.offset = 0;
    zone.delaySeconds = 0.01f;
    voice.start(zone, 60, 127, rate);
    voice.fadeOut();                                             // choked before it began
    const auto choked = render(voice, 960);
    CHECK(!voice.active());
    for (const float x : choked.left) CHECK(x == 0.0f);
}

TEST(sampler, sampler_silent_zone_chokes_without_starting_a_voice)
{
    auto open = keyZone(46); open.group = 2; open.offBy = 1;
    SampleZone mute;                                             // sample=*silence
    mute.silent = true;
    mute.loKey = mute.hiKey = 44;
    mute.group = 1;
    SampleZone empty;                                            // no audio and not silent: dropped
    empty.loKey = empty.hiKey = 50;
    Sampler sampler;
    CHECK(sampler.setZones({open, mute, empty}) == 1);
    sampler.noteOn(46, 100);
    sampler.noteOn(44, 100);
    CHECK(countVoices(sampler, 46, true) == 1);                  // the open hat is choked
    CHECK(countVoices(sampler, 44) == 0 && countVoices(sampler, 44, true) == 0);
    CHECK(sampler.activeVoices() == 1);
}

// ---------------------------------------------------------------- handing zones to a live sampler

TEST(sampler, staged_zones_reach_a_playing_sampler_and_the_old_set_is_freed)
{
    Sampler sampler;
    sampler.prepare(rate, 64);
    sampler.setZones({keyZone(60)});
    Block block(2, 64);
    block.event(0, 0x90, 60, 100);
    block.run(sampler);
    block.midi.clear();
    CHECK(countVoices(sampler, 60) == 1);

    CHECK(sampler.collectRetired() == 0);
    CHECK(sampler.stageZones({keyZone(62, 0.5f)}) == 0);
    CHECK(sampler.zonesPending());
    CHECK(countVoices(sampler, 60) == 1);                        // nothing changes before the next block
    block.run(sampler);
    CHECK(!sampler.zonesPending());
    CHECK(countVoices(sampler, 60, true) == 1);                  // the old note fades out
    for (int i = 0; i < 10; ++i) block.run(sampler);             // 10 ms: the fade is over
    CHECK(sampler.activeVoices() == 0);
    block.run(sampler);                                          // the old set is handed back
    CHECK(sampler.collectRetired() == 1);
    CHECK(sampler.collectRetired() == 0);

    block.event(0, 0x90, 60, 100);                               // the old key has no zone now
    block.event(0, 0x90, 62, 100);
    block.run(sampler);
    CHECK(countVoices(sampler, 60) == 0 && countVoices(sampler, 62) == 1);
    CHECK(sampler.zones().size() == 1 && sampler.zones().zone(0).loKey == 62);
}

TEST(sampler, a_newer_staged_set_replaces_one_not_taken_yet)
{
    Sampler sampler;
    sampler.prepare(rate, 64);
    sampler.stageZones({keyZone(40)});
    sampler.stageZones({keyZone(41)});
    Block block(2, 64);
    block.event(0, 0x90, 40, 100);
    block.event(0, 0x90, 41, 100);
    block.run(sampler);
    CHECK(countVoices(sampler, 40) == 0 && countVoices(sampler, 41) == 1);
}

TEST(sampler, staging_from_another_thread_while_blocks_play)
{
    Sampler sampler;
    sampler.prepare(rate, 64);
    sampler.setZones({keyZone(60)});
    std::atomic<bool> done{false};
    std::thread control([&] {
        for (int i = 0; i < 200; ++i) sampler.stageZones({keyZone(60 + i % 3, 0.25f), keyZone(36)});
        done = true;
    });
    Block block(2, 64);
    int blocks = 0;
    while (!done || blocks < 2000) {
        block.event(0, 0x90, std::uint8_t(60 + blocks % 3), 100);
        if (blocks % 7 == 0) block.event(1, 0x90, 36, 90);
        for (auto& channel : block.data) std::fill(channel.begin(), channel.end(), 0.0f);
        block.run(sampler);
        block.midi.clear();
        for (const float x : block.data[0]) CHECK(std::isfinite(x));
        ++blocks;
        if (blocks > 200000) break;
    }
    control.join();
    for (int i = 0; i < 50; ++i) { block.run(sampler); sampler.collectRetired(); }
    CHECK(!sampler.zonesPending());
    CHECK(sampler.zones().size() == 2);
}
