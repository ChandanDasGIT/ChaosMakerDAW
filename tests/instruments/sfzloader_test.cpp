#include "testkit.h"
#include "instruments/sampler/sampler.h"
#include "instruments/sfz/sfzloader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using sfz::loadSfz;
using sfz::parseKey;

namespace {

namespace fs = std::filesystem;

void put16(std::vector<std::uint8_t>& b, std::uint32_t v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
void tag(std::vector<std::uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A mono 16-bit 44.1 kHz WAV of `frames` frames at half level; with a loop from 10 to 89.
void writeWav(const fs::path& path, int frames = 200, bool loop = false)
{
    std::vector<std::uint8_t> b;
    tag(b, "RIFF"); put32(b, 0); tag(b, "WAVE");
    tag(b, "fmt "); put32(b, 16); put16(b, 1); put16(b, 1); put32(b, 44100); put32(b, 88200); put16(b, 2); put16(b, 16);
    tag(b, "data"); put32(b, std::uint32_t(frames * 2));
    for (int i = 0; i < frames; ++i) put16(b, i % 2 ? 16384u : std::uint32_t(std::uint16_t(-16384)));
    if (loop) {
        tag(b, "smpl"); put32(b, 60);
        for (int i = 0; i < 7; ++i) put32(b, 0);                     // maker .. SMPTE offset
        put32(b, 1); put32(b, 0);                                    // one loop, no extra data
        put32(b, 0); put32(b, 0); put32(b, 10); put32(b, 89); put32(b, 0); put32(b, 0);
    }
    const auto size = std::uint32_t(b.size() - 8);
    for (int i = 0; i < 4; ++i) b[std::size_t(4 + i)] = std::uint8_t(size >> (8 * i));
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

void writeText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A fresh folder for one test, removed when it ends.
struct Folder {
    fs::path path;
    explicit Folder(const char* name) : path(fs::temp_directory_path() / (std::string("chaosmaker-sfzloader-") + name))
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Folder() { fs::remove_all(path); }
};

} // namespace

TEST(sfzloader, key_names_and_numbers)
{
    CHECK(parseKey("c4") == 60 && parseKey("C4") == 60 && parseKey("c#4") == 61 && parseKey("db4") == 61);
    CHECK(parseKey("a0") == 21 && parseKey("c-1") == 0 && parseKey("g9") == 127 && parseKey("b3") == 59);
    CHECK(parseKey("021") == 21 && parseKey("127") == 127 && parseKey("-1") == -1);
    CHECK(parseKey("128") < -1 && parseKey("h4") < -1 && parseKey("") < -1 && parseKey("c") < -1 && parseKey("60.5") < -1);
}

TEST(sfzloader, regions_become_zones_with_their_keys_levels_and_envelopes)
{
    Folder dir("zones");
    writeWav(dir.path / "Samples" / "Kick 1.wav");
    writeWav(dir.path / "Samples" / "snare.wav");
    writeText(dir.path / "Programs" / "kit.sfz", R"(
<control> default_path=..\Samples\
<global> volume=-6 ampeg_release=0.3 seq_length=2
<group> lokey=c2 hikey=d2 pitch_keycenter=36 lovel=1 hivel=100 group_volume=-3 group=1 off_by=2
<region> sample=Kick 1.wav seq_position=2 pan=-50 tune=20 transpose=1 ampeg_attack=0.01 ampeg_decay=0.5 ampeg_sustain=40
<region> sample=Kick 1.wav volume=0 trigger=release
<group>
<region> key=38 sample=snare.wav pitch_keytrack=0 loop_mode=one_shot
)");
    const auto r = loadSfz((dir.path / "Programs" / "kit.sfz").string());
    CHECK(r.report.problems.empty());
    CHECK(r.report.regions == 3 && r.zones.size() == 3 && r.sources.size() == 3);
    CHECK(r.report.samplesRead == 2);                                // Kick 1.wav read once
    if (r.zones.size() != 3) return;
    const auto& a = r.zones[0];
    CHECK(a.audio && a.audio == r.zones[1].audio && a.audio->frames == 200);
    CHECK(a.loKey == 36 && a.hiKey == 38 && a.rootKey == 36 && a.loVelocity == 1 && a.hiVelocity == 100);
    CHECK_NEAR(a.volumeDb, -9.0, 1e-4);                              // volume + group_volume
    CHECK_NEAR(a.pan, -0.5, 1e-6);
    CHECK_NEAR(a.tuneCents, 120.0, 1e-4);
    CHECK(a.roundRobinLength == 2 && a.roundRobinPosition == 2 && a.group == 1 && a.offBy == 2);
    CHECK_NEAR(a.attack, 0.01, 1e-6); CHECK_NEAR(a.decay, 0.5, 1e-6); CHECK_NEAR(a.sustain, 0.4, 1e-6);
    CHECK_NEAR(a.release, 0.3, 1e-6);
    CHECK(!a.oneShot && !a.looping && !a.releaseTrigger);
    CHECK_NEAR(r.zones[1].volumeDb, -3.0, 1e-4);                     // its own volume wins over the global one
    CHECK(r.zones[1].releaseTrigger && r.zones[1].roundRobinPosition == 1);
    const auto& s = r.zones[2];
    CHECK(s.loKey == 38 && s.hiKey == 38 && s.rootKey == 38 && s.oneShot && s.group == 0);
    CHECK_NEAR(s.volumeDb, -6.0, 1e-4);
    CHECK(r.sources[0].sample == (dir.path / "Samples" / "Kick 1.wav").string());
    CHECK(r.sources[2].line == 8);
}

TEST(sfzloader, controllers_at_their_defaults_pick_regions_and_set_levels)
{
    Folder dir("cc");
    writeWav(dir.path / "a.wav");
    writeText(dir.path / "kit.sfz", R"(
<control> set_cc105=80 set_cc100=127 set_hdcc7=0.5
<curve> curve_index=10 v000=0 v063=1 v127=1
<global> key=40 sample=a.wav
<master> locc105=64
<region> amplitude_cc100=50
<master> locc105=96
<region>
<master>
<region> amplitude_cc7=100 amplitude_curvecc7=10 pan_oncc7=100 pan_curvecc7=1 tune_cc7=200
<region> amplitude_cc113=100
<region> key=127 ampeg_decay=80 ampeg_decay_oncc133=-60 ampeg_release=0.5 ampeg_release_oncc72=2
)");
    const auto r = loadSfz((dir.path / "kit.sfz").string());
    CHECK(r.report.problems.empty());
    CHECK(r.report.regions == 5 && r.zones.size() == 3);
    CHECK(r.report.leftOutByControllers == 2);                      // locc105=96, and cc113 at 0 is silent
    if (r.zones.size() != 3) return;
    CHECK_NEAR(r.zones[0].volumeDb, 20.0 * std::log10(0.5), 1e-4);  // 50 % of cc100 at 127
    CHECK_NEAR(r.zones[1].volumeDb, 0.0, 0.1);                      // its curve is full from 63 up
    CHECK_NEAR(r.zones[1].pan, 0.0, 0.01);                          // bipolar curve at the middle
    CHECK_NEAR(r.zones[1].tuneCents, 100.0, 0.1);
    CHECK_NEAR(r.zones[2].decay, 20.0, 1e-4);                       // cc133 is the key: 80 - 60
    CHECK_NEAR(r.zones[2].release, 0.5, 1e-6);                      // cc72 not set: 0
}

TEST(sfzloader, missing_and_unreadable_samples_and_opcodes_are_reported)
{
    Folder dir("report");
    writeWav(dir.path / "ok.wav");
    writeText(dir.path / "piano.flac", "fLaC");
    writeText(dir.path / "bad.wav", "not a wav");
    writeText(dir.path / "i.sfz", R"(
<region> key=60 sample=ok.wav polyphony=2 amp_veltrack=50 cutoff=1000 region_label=x wobble=3
<region> key=61 sample=gone.wav
<region> key=62 sample=piano.flac
<region> key=63 sample=bad.wav
<region> key=64 sample=ok.wav cutoff_cc1=20 trigger=first
<region> key=65
<region> key=66 sample=*sine
<region> lokey=-1 sample=ok.wav
<region> key=67 sample=ok.wav volume=loud
)");
    const auto path = (dir.path / "i.sfz").string();
    const auto r = loadSfz(path);
    CHECK(r.zones.size() == 2);
    CHECK(r.report.missingSamples.size() == 1 && r.report.missingSamples[0].ends_with("gone.wav"));
    CHECK(r.report.unreadableSamples.size() == 2);
    for (const auto& [file, why] : r.report.unreadableSamples) {
        if (file.ends_with("piano.flac")) CHECK(why.find("C4") != std::string::npos);
        else CHECK(file.ends_with("bad.wav") && why.find("bad.wav") != std::string::npos);
    }
    CHECK(r.report.leftOutOther == 7);
    CHECK(r.report.unusedOpcodes.at("polyphony") == 1 && r.report.unusedOpcodes.at("amp_veltrack") == 1);
    CHECK(r.report.unusedOpcodes.at("cutoff") == 1 && r.report.unusedOpcodes.at("trigger=first") == 1);
    CHECK(r.report.unusedOpcodes.at("cutoff_ccN") == 1);
    CHECK(r.report.unknownOpcodes.size() == 1 && r.report.unknownOpcodes.at("wobble") == 1);
    CHECK(!r.report.unusedOpcodes.contains("region_label"));
    bool noSample = false, badValue = false;
    for (const auto& p : r.report.problems) {
        noSample |= p.message == "region has no sample" && p.line == 7;
        badValue |= p.message.find("bad value for volume") != std::string::npos && p.line == 10;
    }
    CHECK(noSample && badValue);

    // Only checking that files exist: the FLAC counts as there, no audio is read.
    const auto check = loadSfz(path, {sfz::SampleMode::Exists});
    CHECK(check.zones.size() == 4 && check.report.samplesRead == 0);
    for (const auto& z : check.zones) CHECK(!z.audio);
    const auto verify = loadSfz(path, {sfz::SampleMode::Verify});
    CHECK(verify.zones.size() == 2 && verify.report.samplesRead == 1 && !verify.zones[0].audio);
}

TEST(sfzloader, loops_come_from_the_file_or_the_sfz)
{
    Folder dir("loop");
    writeWav(dir.path / "held.wav", 200, true);
    writeWav(dir.path / "plain.wav");
    writeText(dir.path / "l.sfz", R"(
<region> key=60 sample=held.wav
<region> key=61 sample=held.wav loop_mode=no_loop
<region> key=62 sample=plain.wav loop_mode=loop_continuous loop_start=20 loop_end=150
<region> key=63 sample=plain.wav loop_mode=loop_continuous
)");
    const auto r = loadSfz((dir.path / "l.sfz").string());
    CHECK(r.zones.size() == 4);
    if (r.zones.size() != 4) return;
    CHECK(r.zones[0].looping && r.zones[0].loopStart == 10 && r.zones[0].loopEnd == 89);
    CHECK(!r.zones[1].looping);
    CHECK(r.zones[2].looping && r.zones[2].loopStart == 20 && r.zones[2].loopEnd == 150);
    CHECK(!r.zones[3].looping && r.report.problems.size() == 1);   // no loop points anywhere
}

TEST(sfzloader, loaded_zones_play_in_the_sampler)
{
    Folder dir("play");
    writeWav(dir.path / "hit.wav", 4000);
    writeText(dir.path / "p.sfz", "<region> key=36 sample=hit.wav loop_mode=one_shot\n");
    auto r = loadSfz((dir.path / "p.sfz").string());
    Sampler sampler;
    sampler.prepare(48000.0, 256);
    CHECK(sampler.setZones(std::move(r.zones)) == 0);              // none dropped
    sampler.noteOn(36, 127);
    std::vector<float> l(256), rgt(256);
    sampler.render(l.data(), rgt.data(), 256);
    float peak = 0.0f;
    for (float x : l) peak = std::max(peak, std::abs(x));
    CHECK(peak > 0.1f);
}

TEST(sfzloader, silence_velocity_curves_offset_and_delay_reach_the_zones)
{
    Folder dir("drumparts");
    writeWav(dir.path / "hat.wav");
    writeText(dir.path / "k.sfz", R"(
<control> set_cc110=127
<curve> curve_index=10 v000=0 v127=1
<global> loop_mode=one_shot
<group> key=46 hivel=31 amp_velcurve_31=1 group=2 off_by=1
<region> sample=hat.wav offset=7 delay=0.002 offset_cc110=20 delay_oncc110=0.004 delay_curvecc110=10
<region> sample=hat.wav seq_position=1 amp_velcurve_31=1
<group> key=44 group=1 seq_length=1
<region> sample=*silence
<group> key=40 lovel=32 amp_velcurve_63=0.5
<region> sample=hat.wav
)");
    const auto r = loadSfz((dir.path / "k.sfz").string());
    CHECK(r.report.problems.empty());
    CHECK(r.zones.size() == 4 && r.report.leftOutOther == 0);
    CHECK(r.report.unusedOpcodes.empty() && r.report.unknownOpcodes.empty());
    if (r.zones.size() != 4) return;
    const auto& hit = r.zones[0];
    CHECK(hit.offset == 27);                                     // 7 + 20 at cc110 = 127
    CHECK_NEAR(hit.delaySeconds, 0.006, 1e-6);                   // 2 ms + 4 ms on its curve
    CHECK(hit.velocityCurve && hit.velocityCurve == r.zones[1].velocityCurve);   // same points, one curve
    if (hit.velocityCurve) {
        CHECK_NEAR((*hit.velocityCurve)[0], 0.0, 1e-6);
        CHECK_NEAR((*hit.velocityCurve)[31], 1.0, 1e-6);
        CHECK_NEAR((*hit.velocityCurve)[127], 1.0, 1e-6);
        CHECK_NEAR((*hit.velocityCurve)[10], 10.0 / 31.0, 1e-6);
    }
    const auto& mute = r.zones[2];
    CHECK(mute.silent && !mute.audio && mute.loKey == 44 && mute.group == 1);
    CHECK(r.sources[2].sample == "*silence");
    const auto& soft = r.zones[3];                               // 0 at 0, 0.5 at 63, 1 at 127
    CHECK(soft.velocityCurve && soft.velocityCurve != hit.velocityCurve);
    if (soft.velocityCurve) {
        CHECK_NEAR((*soft.velocityCurve)[63], 0.5, 1e-6);
        CHECK_NEAR((*soft.velocityCurve)[95], 0.75, 1e-6);
    }

    Sampler sampler;                                             // the silent zone loads and chokes
    CHECK(sampler.setZones(r.zones) == 0);
    sampler.noteOn(46, 20);
    sampler.noteOn(44, 100);
    int fading = 0;
    for (const auto& v : sampler.voices()) fading += v.active() && v.fading();
    CHECK(fading == 2 && sampler.activeVoices() == 2);           // both hat layers choked, no pedal voice
}
