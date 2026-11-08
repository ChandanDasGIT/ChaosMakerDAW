#include "testkit.h"
#include "instruments/instrumentfactory.h"
#include "instruments/piano/piano.h"
#include "instruments/piano/pianolibrary.h"
#include "instruments/sampler/sampler.h"
#include "session/project.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

// A small piano laid out like the Splendid map: one key (C4) in two layers, the soft one
// filtered, a pedal-only resonance layer, release and resonance on CC 72 / 70, the pedal
// lengthening the release on CC 64, the classic sustain moved to CC 90.
struct FakePiano {
    fs::path folder;
    explicit FakePiano(const char* name) : folder(fs::temp_directory_path() / (std::string("chaosmaker-piano-") + name))
    {
        fs::remove_all(folder);
        writeWav(folder / "Samples" / "soft.wav", 48000);
        writeWav(folder / "Samples" / "loud.wav", 47000);
        writeWav(folder / "Samples" / "res.wav", 46000);
        std::ofstream(folder / piano::Program) << R"(
<control> default_path=Samples/ set_hdcc70=0.5 set_hdcc72=0.35 set_hdcc99=1
<global> ampeg_release=0.5 ampeg_release_oncc72=2 ampeg_releasecc64=15 ampeg_dynamic=1
sustain_cc=90 note_polyphony=1 off_time=0.5 amp_veltrack=100 amp_veltrack_oncc99=-100 amp_veltrack_curvecc99=2
<group> group=1 locc64=65 locc70=1 amplitude_oncc70=200
<region> key=60 sample=res.wav
<group> lovel=1 hivel=40 cutoff=1000
<region> key=60 sample=soft.wav
<group> lovel=41 hivel=127
<region> key=60 sample=loud.wav
)";
    }
    ~FakePiano() { fs::remove_all(folder); }
    sfz::Document document() const { return sfz::parseFile((folder / piano::Program).string()); }
};

const SampleZone* zoneOf(const std::vector<SampleZone>& zones, long long frames)
{
    for (const auto& z : zones)
        if (z.audio && z.audio->frames == frames) return &z;
    return nullptr;
}

// One block through the piano with these events; returns the left channel.
std::vector<float> block(Piano& p, std::vector<MidiEvent> events)
{
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    float* ch[2] = {l.data(), r.data()};
    MidiBuffer midi;
    for (const auto& e : events) midi.add(e);
    ProcessContext c;
    c.audio = {ch, 2, 256};
    c.midi = &midi;
    c.sampleRate = 48000.0;
    p.process(c);
    return l;
}

float peak(const std::vector<float>& v)
{
    float p = 0.0f;
    for (const float x : v) p = std::max(p, std::abs(x));
    return p;
}

} // namespace

TEST(piano, settings_stay_in_range_and_become_the_map_controllers)
{
    piano::Settings s;
    s.volumeDb = 99.0; s.release = -1.0; s.resonance = std::nan(""); s.velocity = 2.0;
    s.brightness = -3.0; s.transpose = 40;
    const auto c = piano::sanitize(s);
    CHECK(c.volumeDb == piano::MaxVolumeDb && c.release == 0.0 && c.resonance == piano::DefaultResonance);
    CHECK(c.velocity == 1.0 && c.brightness == 0.0 && c.transpose == piano::MaxTranspose);
    CHECK(piano::sanitize({}) == piano::Settings{} && !(c == piano::Settings{}));

    const auto cc = piano::controllers({});
    CHECK_NEAR(cc.at(72), 0.35 * 127.0, 1e-9);                     // the map's own defaults
    CHECK_NEAR(cc.at(70), 0.5 * 127.0, 1e-9);
    CHECK_NEAR(cc.at(99), 127.0, 1e-9);
    CHECK(!cc.contains(64));                                       // the pedal stays live

    CHECK(piano::brightnessCutoff(1.0) == 0.0);                    // as recorded
    CHECK_NEAR(piano::brightnessCutoff(0.0), 1000.0, 1e-6);
    CHECK(piano::brightnessCutoff(0.99) > 18000.0);
    CHECK(piano::brightnessCutoff(0.3) < piano::brightnessCutoff(0.6));
}

TEST(piano, zones_follow_the_settings_and_keep_the_pedal_live)
{
    FakePiano fake("zones");
    const auto map = fake.document();
    auto cache = std::make_shared<sfz::SampleCache>();
    const auto plain = piano::buildZones(map, {}, cache);
    CHECK(plain.size() == 3);
    const auto* res = zoneOf(plain, 46000);
    const auto* soft = zoneOf(plain, 48000);
    const auto* loud = zoneOf(plain, 47000);
    CHECK(res && soft && loud);
    if (!res || !soft || !loud) return;
    CHECK(res->liveCc == 64 && res->liveLo == 65);                 // only with the pedal down
    CHECK(soft->cutoffHz == 1000.0f && loud->cutoffHz == 0.0f);
    CHECK(loud->releaseCc == 64 && loud->dynamicRelease && loud->sustainCc == 90 && loud->notePolyphony == 1);
    CHECK_NEAR(loud->release, 0.5 + 2.0 * 0.35, 1e-4);             // release 0.35 of 2 s
    CHECK_NEAR(loud->releaseFor(127), 0.5 + 0.7 + 15.0, 1e-3);
    CHECK_NEAR(res->volumeDb, piano::TrimDb, 1e-3);                // resonance 0.5: 200 % x 0.5, then the trim

    piano::Settings s;
    s.transpose = 2;                                               // C4 sounds D4: key 58 plays it
    s.volumeDb = -6.0;
    s.brightness = 0.0;
    s.release = 1.0;
    s.resonance = 0.25;
    const auto changed = piano::buildZones(map, s, cache);
    const auto* l2 = zoneOf(changed, 47000);
    const auto* s2 = zoneOf(changed, 48000);
    const auto* r2 = zoneOf(changed, 46000);
    CHECK(l2 && s2 && r2);
    if (!l2 || !s2 || !r2) return;
    CHECK(l2->loKey == 58 && l2->hiKey == 58 && l2->rootKey == 58);
    CHECK_NEAR(l2->volumeDb, piano::TrimDb - 6.0, 1e-3);
    CHECK(l2->cutoffHz == 1000.0f && s2->cutoffHz == 1000.0f);     // brightness 0: 1 kHz, soft stays 1 kHz
    CHECK_NEAR(l2->release, 0.5 + 2.0, 1e-3);
    CHECK_NEAR(r2->volumeDb, piano::TrimDb - 6.0 + 20.0 * std::log10(0.5), 1e-2);   // half the resonance
    s.resonance = 0.0;                                             // none: the layer is left out
    CHECK(piano::buildZones(map, s, cache).size() == 2);
}

TEST(piano, the_instrument_loads_plays_and_shows_its_keys_and_pedal)
{
    FakePiano fake("instrument");
    auto library = std::make_shared<piano::Library>(fake.folder.string());
    Piano p(library);
    p.prepare(48000.0, 256);
    library->waitUntilIdle();
    CHECK(library->state() == piano::Library::State::Ready && library->message().empty());
    CHECK(p.typeId() == "piano");

    const auto out = block(p, {{0, 0x90, 60, 100}});               // the first block takes the zones
    CHECK(p.sampler().zones().size() == 3);
    CHECK(peak(out) > 0.1f);
    CHECK(p.keyDown(60) && p.keyVelocity(60) == 100 && p.presses(60) == 1);
    CHECK(p.sampler().activeVoices() == 1);                        // no pedal: no resonance
    block(p, {{0, 0xB0, 64, 127}, {1, 0x80, 60, 0}});
    CHECK(!p.keyDown(60) && p.pedal() == 127);
    CHECK(p.sampler().activeVoices() == 1);                        // the pedal keeps it ringing
    block(p, {{0, 0x90, 60, 30}});                                 // struck again, pedal down
    CHECK(p.presses(60) == 2 && p.keyVelocity(60) == 30);
    int fresh = 0;
    for (const auto& v : p.sampler().voices()) fresh += v.active() && !v.fading();
    CHECK(fresh == 2);                                             // soft string + resonance; the old one fades
    block(p, {{0, 0xB0, 123, 0}});
    CHECK(!p.keyDown(60));

    piano::Settings s;
    s.transpose = -12;
    p.setSettings(s);
    library->waitUntilIdle();
    block(p, {});
    CHECK(p.settings().transpose == -12);
    CHECK(p.sampler().zones().zone(0).loKey == 72);                // C5 plays the C4 sample an octave down
}

TEST(piano, a_missing_piano_says_where_it_looked_and_stays_silent)
{
    auto library = std::make_shared<piano::Library>("/nonexistent/chaosmaker/piano");
    Piano p(library);
    p.prepare(48000.0, 256);
    library->waitUntilIdle();
    CHECK(library->state() == piano::Library::State::Missing);
    CHECK(library->message().find("/nonexistent/chaosmaker/piano") != std::string::npos);
    CHECK(peak(block(p, {{0, 0x90, 60, 100}})) == 0.0f);
    CHECK(p.keyDown(60));                                          // the keys still show
}

TEST(piano, piano_tracks_get_the_piano_and_its_scale_changer)
{
    const auto instrument = createInstrument("Piano");
    CHECK(instrument && instrument->typeId() == "piano");
    // backendtests points CHAOSMAKER_SAMPLE_KITS at a folder that does not exist.
    CHECK(piano::findFolder().starts_with("/nonexistent/chaosmaker-tests/sample-kits/"));
    CHECK(piano::findFolder().ends_with(piano::FolderName));

    Project project;
    auto& track = project.addTrack(TrackKind::Instrument, "Keys", "Piano");
    CHECK(track.keyShift() == 0);
    piano::Settings s;
    s.transpose = -3;
    CHECK(project.setTrackPiano(track.id, s));
    CHECK(project.findTrack(track.id)->keyShift() == -3);
    s.transpose = 50;                                              // clamped
    CHECK(project.setTrackPiano(track.id, s));
    CHECK(project.findTrack(track.id)->piano.transpose == piano::MaxTranspose);
    CHECK(!project.setTrackPiano(999, s));
}
