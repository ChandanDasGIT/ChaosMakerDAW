#include "testkit.h"
#include "instruments/drums/drumkit.h"
#include "instruments/drums/drumkitlibrary.h"
#include "instruments/instrumentfactory.h"
#include "instruments/sampler/sampler.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

void put16(std::vector<std::uint8_t>& b, std::uint32_t v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
void tag(std::vector<std::uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A mono 16-bit WAV at half level; its length tells the samples apart.
void writeWav(const fs::path& path, int frames)
{
    std::vector<std::uint8_t> b;
    tag(b, "RIFF"); put32(b, 36 + std::uint32_t(frames) * 2); tag(b, "WAVE");
    tag(b, "fmt "); put32(b, 16); put16(b, 1); put16(b, 1); put32(b, 48000); put32(b, 96000); put16(b, 2); put16(b, 16);
    tag(b, "data"); put32(b, std::uint32_t(frames * 2));
    for (int i = 0; i < frames; ++i) put16(b, 16384u);
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

// The sample lengths of the fake kit.
enum Len { Kick = 1000, KickFront = 1100, Tom = 1200, Tight = 1300, Pedal = 1400,
           Open = 1500, Half = 1600, Loose = 1700, Closed = 1800 };

// A small kit laid out like Gogodze Phu Vol II: mics picked by CC 105, a tom thud layer on
// CC 114, tuning on CC 108, hi-hats on key 46 picked by CC 4, mutes that choke open hats.
struct FakeKit {
    fs::path folder;
    explicit FakeKit(const char* name) : folder(fs::temp_directory_path() / (std::string("chaosmaker-drumkit-") + name))
    {
        fs::remove_all(folder);
        const auto samples = folder / "Samples";
        writeWav(samples / "kick.wav", Kick);
        writeWav(samples / "kick_front.wav", KickFront);
        writeWav(samples / "tom.wav", Tom);
        writeWav(samples / "tight.wav", Tight);
        writeWav(samples / "pedal.wav", Pedal);
        writeWav(samples / "open.wav", Open);
        writeWav(samples / "half.wav", Half);
        writeWav(samples / "loose.wav", Loose);
        writeWav(samples / "closed.wav", Closed);
        fs::create_directories(folder / "Programs");
        std::ofstream(folder / "Programs" / "Kit.sfz") << R"(
<control> set_cc105=80 set_cc108=63 set_cc110=63 set_cc111=127
<global> loop_mode=one_shot pitch_keytrack=0 tune=-1200 tune_cc108=2400
<master>
<region> key=36 sample=../Samples/kick.wav
<master> locc105=64
<region> key=36 sample=../Samples/kick_front.wav
<master> amplitude_cc114=44 locc114=1 transpose=-12
<region> key=41 sample=../Samples/tom.wav
<master>
<region> key=41 sample=../Samples/tom.wav
<region> key=42 sample=../Samples/tight.wav group=24
<region> key=44 sample=../Samples/pedal.wav
<group> group=27 off_by=21 locc4=96
<region> key=46 sample=../Samples/open.wav
<group> group=26 off_by=21 locc4=64 hicc4=95
<region> key=46 sample=../Samples/half.wav
<group> group=25 locc4=32 hicc4=63
<region> key=46 sample=../Samples/loose.wav
<group> group=23 hicc4=31
<region> key=46 sample=../Samples/closed.wav
<group> group=21 sample=*silence
<region> key=42
<region> key=44
<region> key=46 hicc4=95
)";
    }
    ~FakeKit() { fs::remove_all(folder); }
    sfz::Document document() const { return sfz::parseFile((folder / "Programs" / "Kit.sfz").string()); }
};

// key -> sample lengths (0 for a silent zone)
std::map<int, std::multiset<long long>> byKey(const std::vector<SampleZone>& zones)
{
    std::map<int, std::multiset<long long>> out;
    for (const auto& z : zones) {
        CHECK(z.loKey == z.hiKey);
        out[z.loKey].insert(z.silent ? 0 : (long long)z.audio->frames);
    }
    return out;
}

const SampleZone* zoneOf(const std::vector<SampleZone>& zones, long long frames)
{
    for (const auto& z : zones)
        if (!z.silent && z.audio->frames == frames) return &z;
    return nullptr;
}

void play(DrumKit& kit, std::initializer_list<int> keys, std::vector<float>* out = nullptr)
{
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    float* ch[2] = {l.data(), r.data()};
    MidiBuffer midi;
    for (int key : keys) midi.add({0, 0x90, std::uint8_t(key), 100});
    ProcessContext c;
    c.audio = {ch, 2, 256};
    c.midi = &midi;
    c.sampleRate = 48000.0;
    kit.process(c);
    if (out) *out = l;
}

} // namespace

TEST(drumkit, settings_become_the_kit_controllers)
{
    const auto d = drumkit::controllers({});
    CHECK_NEAR(d.at(105), 80.0, 1e-9);                            // the front room mic, as the kit sets it
    CHECK_NEAR(d.at(108), 63.5, 1e-9);                            // in tune
    CHECK_NEAR(d.at(110), 63.0, 1e-9);                            // the kit's own mic timing
    CHECK_NEAR(d.at(111), 127.0, 1e-9);
    CHECK_NEAR(d.at(114), 0.0, 1e-9);
    drumkit::Settings s;
    s.room = drumkit::Room::Full; s.tuneSemitones = -12.0; s.tomThud = 1.0;
    const auto c = drumkit::controllers(s);
    CHECK_NEAR(c.at(105), 127.0, 1e-9);
    CHECK_NEAR(c.at(108), 0.0, 1e-9);
    CHECK_NEAR(c.at(114), 127.0, 1e-9);
    s.room = drumkit::Room::Close;
    CHECK_NEAR(drumkit::controllers(s).at(105), 0.0, 1e-9);

    drumkit::Settings wild;
    wild.kickDb = 99.0; wild.tuneSemitones = -40.0; wild.micTiming = std::nan("");
    const auto clean = drumkit::sanitize(wild);
    CHECK(clean.kickDb == drumkit::MaxLevelDb && clean.tuneSemitones == -drumkit::MaxTuneSemitones);
    CHECK(clean.micTiming == drumkit::DefaultMicTiming);
    drumkit::Room room = drumkit::Room::Close;
    CHECK(drumkit::roomFromName("full", room) && room == drumkit::Room::Full);
    CHECK(drumkit::roomTrimDb(drumkit::Room::Full) < drumkit::roomTrimDb(drumkit::Room::Front));
    CHECK(drumkit::roomTrimDb(drumkit::Room::Front) < drumkit::roomTrimDb(drumkit::Room::Close));
    CHECK(!drumkit::roomFromName("hall", room) && drumkit::roomName(drumkit::Room::Front) == "front");
}

TEST(drumkit, hihat_articulations_move_to_their_own_keys_with_their_chokes)
{
    FakeKit kit("keys");
    sfz::LoadReport report;
    const auto zones = drumkit::buildZones(kit.document(), {}, std::make_shared<sfz::SampleCache>(), &report);
    CHECK(report.problems.empty() && report.missingSamples.empty());
    const auto keys = byKey(zones);
    using S = std::multiset<long long>;
    CHECK(keys.at(60) == S({Kick, KickFront}));                   // front room mic at the default mix
    CHECK(keys.at(65) == S({Tom}));                               // no thud layer by default
    CHECK(keys.at(55) == S({0, Tight}));                          // the kit's tight hat (its 42)
    CHECK(keys.at(66) == S({0, Closed}));                         // closed, with the mute
    CHECK(keys.at(56) == S({0, Loose}));
    CHECK(keys.at(57) == S({0, Half}));
    CHECK(keys.at(70) == S({Open}));                              // open mutes nothing
    CHECK(keys.at(68) == S({0, Pedal}));
    CHECK(keys.size() == 8);

    const auto* closed = zoneOf(zones, Closed);
    CHECK(closed && closed->rootKey == 66);                       // no pitch shift from the move
    const auto* kickZone = zoneOf(zones, Kick);
    CHECK(kickZone && kickZone->rootKey == 60);
    CHECK(closed && std::abs(closed->tuneCents) < 1e-3f);

    Sampler sampler;                                              // closed and pedal choke the open hat
    CHECK(sampler.setZones(zones) == 0);
    sampler.noteOn(70, 100);
    sampler.noteOn(66, 100);
    int fading = 0;
    for (const auto& v : sampler.voices()) fading += v.active() && v.fading();
    CHECK(fading == 1);
}

TEST(drumkit, room_tuning_thud_and_levels_reach_the_zones)
{
    FakeKit kit("settings");
    const auto doc = kit.document();
    const auto cache = std::make_shared<sfz::SampleCache>();
    drumkit::Settings s;
    s.room = drumkit::Room::Close;
    s.tuneSemitones = 2.0;
    s.tomThud = 1.0;
    s.kickDb = -6.0;
    s.volumeDb = 3.0;
    const auto zones = drumkit::buildZones(doc, s, cache);
    CHECK(byKey(zones).at(60) == std::multiset<long long>({Kick}));   // close mics only
    CHECK(byKey(zones).at(65).size() == 2);                       // the thud layer joins
    const auto* kick = zoneOf(zones, Kick);
    CHECK(kick && std::abs(kick->tuneCents - 200.0f) < 0.01f);
    const float trim = float(drumkit::roomTrimDb(drumkit::Room::Close));
    CHECK(kick && std::abs(kick->volumeDb - (trim - 3.0f)) < 1e-4f);   // room trim, -6 dB kick, +3 dB volume
    int thud = 0;
    for (const auto& z : zones) if (z.loKey == 65 && std::abs(z.tuneCents - (-1000.0f)) < 0.01f) ++thud;
    CHECK(thud == 1);                                             // an octave down, plus the kit's tuning
    const auto* tight = zoneOf(zones, Tight);
    CHECK(tight && std::abs(tight->volumeDb - (trim + 3.0f)) < 1e-4f);   // hats: trim and volume only

    s.room = drumkit::Room::Full;
    CHECK(byKey(drumkit::buildZones(doc, s, cache)).at(60).size() == 2);
    CHECK(cache->entries.size() == 9);                            // each sample read once
}

TEST(drumkit, the_instrument_loads_in_the_background_and_rebuilds_for_new_settings)
{
    FakeKit kit("instrument");
    auto library = std::make_shared<drumkit::Library>(kit.folder.string());
    DrumKit drums(library);
    drums.prepare(48000.0, 256);
    library->waitUntilIdle();
    CHECK(library->state() == drumkit::Library::State::Ready && library->message().empty());

    std::vector<float> out;
    play(drums, {60}, &out);                                      // the first block takes the zones
    CHECK(drums.sampler().zones().size() == 14);
    CHECK(out[10] > 0.1f);
    CHECK(drums.typeId() == "drumkit");

    drumkit::Settings s;
    s.room = drumkit::Room::Close;
    drums.setSettings(s);
    library->waitUntilIdle();
    play(drums, {});
    CHECK(drums.sampler().zones().size() == 13);                  // the front mic is gone
    CHECK(drums.settings().room == drumkit::Room::Close);

    DrumKit second(library);                                      // a second track shares the samples
    library->waitUntilIdle();
    play(second, {62});
    CHECK(second.sampler().zones().size() == 14);
}

TEST(drumkit, a_missing_kit_says_where_it_looked_and_stays_silent)
{
    auto library = std::make_shared<drumkit::Library>("/nonexistent/chaosmaker/kit");
    DrumKit drums(library);
    drums.prepare(48000.0, 256);
    library->waitUntilIdle();
    CHECK(library->state() == drumkit::Library::State::Missing);
    CHECK(library->message().find("/nonexistent/chaosmaker/kit") != std::string::npos);
    std::vector<float> out;
    play(drums, {60}, &out);
    for (const float x : out) CHECK(x == 0.0f);
    drums.setSettings({});                                         // no crash, still missing
    library->waitUntilIdle();
    CHECK(library->state() == drumkit::Library::State::Missing);
}

TEST(drumkit, drums_tracks_get_the_kit_and_the_folder_comes_from_the_environment)
{
    const auto instrument = createInstrument("Drums");
    CHECK(instrument && instrument->typeId() == "drumkit");
    // backendtests points CHAOSMAKER_SAMPLE_KITS at a folder that does not exist.
    const std::string folder = drumkit::findKitFolder();
    CHECK(folder.starts_with("/nonexistent/chaosmaker-tests/sample-kits/"));
    CHECK(folder.ends_with(drumkit::KitFolderName));
}

TEST(drumkit, the_kit_counts_its_hits_per_key_for_the_picture)
{
    FakeKit kit("hits");
    auto library = std::make_shared<drumkit::Library>(kit.folder.string());
    DrumKit drums(library);
    drums.prepare(48000.0, 256);
    library->waitUntilIdle();
    CHECK(drums.hits(60).count == 0);
    play(drums, {60, 62});
    play(drums, {60});
    CHECK(drums.hits(60).count == 2 && drums.hits(60).velocity == 100);
    CHECK(drums.hits(62).count == 1 && drums.hits(64).count == 0);

    std::vector<float> l(64), r(64);                                   // note-offs and velocity 0 do not count
    float* ch[2] = {l.data(), r.data()};
    MidiBuffer midi;
    midi.add({0, 0x80, 60, 0});
    midi.add({1, 0x90, 60, 0});
    midi.add({2, 0x90, 66, 37});
    ProcessContext c;
    c.audio = {ch, 2, 64};
    c.midi = &midi;
    c.sampleRate = 48000.0;
    drums.process(c);
    CHECK(drums.hits(60).count == 2);
    CHECK(drums.hits(66).count == 1 && drums.hits(66).velocity == 37);
    CHECK(drums.hits(-1).count == 0 && drums.hits(128).count == 0);
}

TEST(drumkit, samples_no_track_plays_are_dropped_from_the_cache)
{
    FakeKit kit("trim");
    auto library = std::make_shared<drumkit::Library>(kit.folder.string());
    {
        DrumKit drums(library);
        drums.prepare(48000.0, 256);
        drumkit::Settings s;
        s.room = drumkit::Room::Full;
        drums.setSettings(s);
        library->waitUntilIdle();
        play(drums, {});
        play(drums, {});
        library->trim();
        CHECK(library->cachedSamples() == 9);                     // the front mic's kick too

        s.room = drumkit::Room::Close;
        drums.setSettings(s);
        library->waitUntilIdle();
        CHECK(library->cachedSamples() == 9);                     // the old zones still play
        play(drums, {});                                          // the audio thread takes the new set
        play(drums, {});                                          // and retires the old one
        library->trim();
        CHECK(library->cachedSamples() == 8);                     // the front mic's kick is gone
        std::vector<float> out;
        play(drums, {60}, &out);
        CHECK(out[10] > 0.1f);                                    // the close kick still plays

        DrumKit second(library);                                  // another track keeps what it uses
        second.prepare(48000.0, 256);
        second.setSettings(s);
        library->waitUntilIdle();
        play(second, {});
        play(second, {});
        library->trim();
        CHECK(library->cachedSamples() == 8);

        s.room = drumkit::Room::Front;                            // choosing it again reads it again
        drums.setSettings(s);
        library->waitUntilIdle();
        CHECK(library->cachedSamples() == 9);
        play(drums, {});
    }
    library->trim();                                              // no Drums track left
    CHECK(library->cachedSamples() == 0);
}
