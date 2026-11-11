#include "testkit.h"
#include "plugins/mixfx/mixfxfactory.h"
#include "plugins/pitchmonitor/pitchmonitor.h"
#include "persistence/mixfxjson.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace {

// Plays `seconds` of a 220 Hz tone through the effect from song sample `position`, 256 at a time.
// Returns whether the output equalled the input throughout.
bool play(PitchMonitor& fx, std::int64_t position, double seconds, bool playing = true)
{
    bool same = true;
    const auto total = std::int64_t(seconds * 48000);
    for (std::int64_t at = 0; at < total; at += 256) {
        std::vector<float> l(256), r(256);
        for (int i = 0; i < 256; ++i)
            l[std::size_t(i)] = r[std::size_t(i)] = float(0.3 * std::sin(2 * std::numbers::pi * 220.0 * double(at + i) / 48000.0));
        const auto inL = l, inR = r;
        float* ch[2] = {l.data(), r.data()};
        ProcessContext c;
        c.audio = {ch, 2, 256};
        c.sampleRate = 48000;
        c.samplePosition = playing ? position + at : position;
        c.playing = playing;
        fx.process(c);
        same = same && l == inL && r == inR;
    }
    return same;
}

} // namespace

TEST(pitchmonitor, passes_the_sound_and_reads_its_pitch_at_the_song_position)
{
    auto created = mixfx::create(mixfx::PitchMonitor);
    auto fx = std::dynamic_pointer_cast<PitchMonitor>(created);
    CHECK(fx && fx->typeId() == "mix.pitchmonitor" && fx->latencySamples() == 0);
    fx->prepare(48000, 256);

    CHECK(play(*fx, 96000, 0.3));                                  // not listening: nothing measured
    PitchMonitor::Reading reading;
    CHECK(!fx->read(reading));

    fx->setListening(true);
    CHECK(play(*fx, 96000, 0.5));                                  // the sound is untouched
    std::vector<PitchMonitor::Reading> readings;
    while (fx->read(reading)) readings.push_back(reading);
    CHECK(readings.size() >= 40 && readings.size() <= 50);
    for (std::size_t i = 1; i < readings.size(); ++i) CHECK(readings[i].songSample - readings[i - 1].songSample == 480);
    // The same frames as the whole analysis of that sound, placed where it played.
    std::vector<float> mono(24000);
    for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = float(0.3 * std::sin(2 * std::numbers::pi * 220.0 * double(i) / 48000.0));
    const auto whole = pitch::Tracker::analyse(mono, 48000);
    CHECK(!readings.empty() && readings.front().songSample == 96000 + whole.front().sample);
    for (const auto& r : readings) {
        CHECK(r.playing);
        CHECK(r.hz > 0.0f && std::abs(1200.0 * std::log2(r.hz / 220.0)) < 3.0);
    }

    // Stopped: readings keep coming (the page's "now" readout), flagged as not playing.
    CHECK(play(*fx, 5000, 0.1, false));
    int stopped = 0;
    while (fx->read(reading)) { CHECK(!reading.playing && reading.songSample <= 5000); ++stopped; }
    CHECK(stopped > 0);

    // Bypassed: the sound still passes.
    mixfx::Settings off = mixfx::defaults(mixfx::PitchMonitor);
    off.bypassed = true;
    fx->apply(off);
    CHECK(play(*fx, 0, 0.1));
}

TEST(pitchmonitor, the_key_is_a_tonic_and_a_scale_chromatic_until_chosen)
{
    const auto* spec = mixfx::find(mixfx::PitchMonitor);
    CHECK(spec && spec->title == "Pitch Monitor" && !spec->meter);
    const auto defaults = mixfx::defaults(mixfx::PitchMonitor);
    CHECK(defaults.values.at("key") == 0.0f && defaults.values.at("scale") == 5.0f);   // C, Chromatic
    mixfx::Settings s;
    s.values = {{"key", 9.4f}, {"scale", 17.0f}, {"other", 1.0f}};
    const auto clean = mixfx::sanitize(mixfx::PitchMonitor, s);
    CHECK(clean.values.at("key") == 9.0f && clean.values.at("scale") == 5.0f && !clean.values.contains("other"));
    const auto* key = mixfx::findControl(*spec, "key");
    CHECK(key && key->choices.size() == 12 && key->choices[9] == "A");

    // Saved with projects and FX chain files like any mixing plugin: A minor comes back.
    auto aMinor = defaults;
    aMinor.values["key"] = 9;
    aMinor.values["scale"] = 1;
    mixfx::Settings back;
    std::string error;
    CHECK(mixfxjson::fromJson(mixfx::PitchMonitor, mixfxjson::toJson(aMinor), back, error) && back == aMinor);
}
