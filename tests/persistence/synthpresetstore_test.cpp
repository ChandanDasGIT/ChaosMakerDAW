#include "testkit.h"
#include "instruments/synth/synth.h"
#include "instruments/synth/synthpresets.h"
#include "persistence/synthjson.h"
#include "persistence/synthpresetstore.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

namespace fs = std::filesystem;
using synth::Preset;

namespace {

// A throwaway folder: never the app's real data folder.
struct TempFolder {
    fs::path path;
    explicit TempFolder(const char* name)
        : path(fs::temp_directory_path() / (std::string("chaosmaker-synthpresets-") + name + "-"
               + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        fs::remove_all(path);
    }
    ~TempFolder() { std::error_code ec; fs::remove_all(path, ec); }
};

Preset userPreset(const char* name, double cutoff = 1234.0)
{
    Preset p;
    p.name = name;
    p.settings.filter.cutoffHz = cutoff;
    p.settings.mode = synth::PlayMode::Mono;
    return p;
}

std::string lower(std::string s)
{
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

TEST(synthpresetstore, eight_factory_presets_with_unique_names_already_in_range)
{
    const auto& presets = synth::factoryPresets();
    CHECK(presets.size() == 8);
    std::set<std::string> names;
    for (const auto& p : presets) {
        CHECK(p.factory);
        CHECK(names.insert(lower(p.name)).second);
        CHECK(synth::cleanPresetName(p.name) == p.name);
        CHECK(synth::sanitize(p.settings) == p.settings);
    }
    CHECK(presets.front().name == "Fat Bass" && presets.back().name == "Wobble");
    CHECK(presets.back().settings.lfo.sync);                       // the tempo-synced one
}

TEST(synthpresetstore, every_factory_preset_sounds_and_a_chord_stays_below_full_scale)
{
    constexpr double rate = 48000.0;
    for (const auto& p : synth::factoryPresets()) {
        Synth s;
        s.prepare(rate, 256);
        s.setSettings(p.settings);
        s.reset();
        MidiBuffer midi;
        for (int key : {48, 55, 60, 64}) midi.add({0, 0x90, std::uint8_t(key), 100});
        double sum = 0.0, peak = 0.0;
        std::size_t n = 0;
        std::vector<float> l(256), r(256);
        for (int block = 0; block < int(1.5 * rate / 256); ++block) {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            float* ch[2] = {l.data(), r.data()};
            ProcessContext c;
            c.audio = {ch, 2, 256};
            c.midi = &midi;
            c.sampleRate = rate;
            c.playing = true;
            c.beat = block * 256 / rate * 2.0;                     // 120 BPM
            s.process(c);
            midi.clear();
            for (float x : l) { sum += double(x) * x; peak = std::max(peak, double(std::abs(x))); ++n; }
        }
        const double rms = std::sqrt(sum / double(n));
        if (!(rms > 0.02 && peak < 1.0)) std::printf("    %s: rms %.3f peak %.3f\n", p.name.c_str(), rms, peak);
        CHECK(rms > 0.02);
        CHECK(peak < 1.0);
    }
}

TEST(synthpresetstore, preset_names_are_trimmed_and_checked)
{
    CHECK(synth::cleanPresetName("  My bass ") == std::optional<std::string>("My bass"));
    CHECK(!synth::cleanPresetName("   "));
    CHECK(!synth::cleanPresetName("a/b"));
    CHECK(!synth::cleanPresetName("a\\b"));
    CHECK(!synth::cleanPresetName("tab\there"));
    CHECK(!synth::cleanPresetName(std::string(49, 'x')));
    CHECK(synth::cleanPresetName(std::string(48, 'x')));
}

TEST(synthpresetstore, preset_files_round_trip_and_bad_ones_are_refused)
{
    using namespace persistence::synthjson;
    const auto p = userPreset("Mine");
    Preset back;
    std::string error;
    CHECK(presetFromJson(presetToJson(p), back, error));
    CHECK(back.name == "Mine" && back.settings == p.settings && !back.factory);

    const auto refused = [&](nlohmann::json j) {
        Preset untouched = userPreset("Untouched");
        const bool ok = presetFromJson(j, untouched, error);
        return !ok && !error.empty() && untouched.name == "Untouched";
    };
    auto j = presetToJson(p);
    CHECK(refused(nlohmann::json::array()));
    { auto b = j; b["format"] = "ChaosMakerGroove"; CHECK(refused(b)); }
    { auto b = j; b["version"] = 2; CHECK(refused(b)); CHECK(error.find("newer") != std::string::npos); }
    { auto b = j; b.erase("name"); CHECK(refused(b)); }
    { auto b = j; b["name"] = "a/b"; CHECK(refused(b)); }
    { auto b = j; b["settings"]["filter"]["cutoff"] = -5; CHECK(refused(b)); }
    { auto b = j; b.erase("settings"); CHECK(refused(b)); }
}

TEST(synthpresetstore, the_store_saves_renames_and_deletes_user_presets_but_never_factory_ones)
{
    TempFolder folder("store");
    SynthPresetStore store(folder.path);
    std::string error;
    CHECK(store.all().size() == 8);                                 // the folder need not exist yet
    CHECK(store.userPresets().empty());

    CHECK(store.save(userPreset("Deep 1"), false, error));
    CHECK(fs::is_directory(folder.path));
    CHECK(fs::exists(folder.path / "deep-1.synthpreset"));
    CHECK(store.userPresets().size() == 1 && store.all().size() == 9);
    CHECK(store.all().back().name == "Deep 1" && !store.all().back().factory);
    CHECK(!store.save(userPreset("deep 1"), false, error));         // names are unique ignoring case
    CHECK(error.find("already exists") != std::string::npos);
    CHECK(store.save(userPreset("DEEP 1", 900.0), true, error));    // replace when asked
    CHECK(store.userPresets().size() == 1);
    CHECK(store.find("deep 1") && store.find("deep 1")->settings.filter.cutoffHz == 900.0);

    auto wild = userPreset("Wild");                                 // stored sanitized
    wild.settings.filter.cutoffHz = 1e9;
    CHECK(store.save(wild, false, error));
    CHECK(store.find("Wild")->settings.filter.cutoffHz == synth::MaxCutoffHz);

    CHECK(!store.save(userPreset("fat bass"), false, error));       // a factory name
    CHECK(!store.save(userPreset("Pluck"), true, error));
    CHECK(!store.save(userPreset("  "), false, error));
    CHECK(!store.rename("Wobble", "Mine", error));
    CHECK(!store.remove("Warm Pad", error));
    CHECK(store.find("warm pad") && store.find("Warm Pad")->factory);

    CHECK(store.save(userPreset("Another"), false, error));
    const auto users = store.userPresets();                         // sorted by name
    CHECK(users.size() == 3 && users[0].name == "Another" && users[1].name == "DEEP 1" && users[2].name == "Wild");

    CHECK(!store.rename("Another", "Wild", error));                 // taken
    CHECK(!store.rename("Another", "Brass", error));                // a factory name
    CHECK(store.rename("Another", "another", error));               // only the case changes
    CHECK(store.rename("another", "Third", error));
    CHECK(!store.find("Another") && store.find("Third"));
    CHECK(store.remove("third", error));
    CHECK(!store.find("Third") && store.userPresets().size() == 2);
    CHECK(!store.remove("Third", error));

    // A broken file or a file using a factory name is skipped, not fatal.
    { std::ofstream(folder.path / "broken.synthpreset") << "{ not json"; }
    { std::ofstream(folder.path / "sneaky.synthpreset") << persistence::synthjson::presetToJson(userPreset("Strings")).dump(); }
    { std::ofstream(folder.path / "other.txt") << "ignored"; }
    CHECK(store.userPresets().size() == 2);
    // A second store on the same folder sees the same presets (they live on disk).
    CHECK(SynthPresetStore(folder.path).userPresets().size() == 2);
}

TEST(synthpresetstore, a_store_without_a_folder_refuses_to_save)
{
    SynthPresetStore store{fs::path()};
    std::string error;
    CHECK(store.all().size() == 8);
    CHECK(!store.save(userPreset("Mine"), false, error) && !error.empty());
}
