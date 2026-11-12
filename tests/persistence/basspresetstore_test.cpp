#include "testkit.h"
#include "instruments/bass/bassguitar.h"
#include "instruments/bass/basspresets.h"
#include "persistence/bassjson.h"
#include "persistence/basspresetstore.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

namespace fs = std::filesystem;
using bass::Preset;

namespace {

// A throwaway folder: never the app's real data folder.
struct TempFolder {
    fs::path path;
    explicit TempFolder(const char* name)
        : path(fs::temp_directory_path() / (std::string("chaosmaker-basspresets-") + name + "-"
               + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        fs::remove_all(path);
    }
    ~TempFolder() { std::error_code ec; fs::remove_all(path, ec); }
};

Preset userPreset(const char* name, double tone = 0.25)
{
    Preset p;
    p.name = name;
    p.settings.tone = tone;
    p.settings.style = bass::Style::Mute;
    return p;
}

std::string lower(std::string s)
{
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

TEST(basspresetstore, six_factory_presets_with_unique_names_already_in_range)
{
    const auto& presets = bass::factoryPresets();
    CHECK(presets.size() == 6);
    std::set<std::string> names;
    for (const auto& p : presets) {
        CHECK(p.factory);
        CHECK(names.insert(lower(p.name)).second);
        CHECK(bass::cleanPresetName(p.name) == p.name);
        CHECK(bass::sanitize(p.settings) == p.settings);
    }
    CHECK(presets.front().name == "Finger P-Bass" && presets.back().name == "Rock Drive");
    CHECK(presets[2].settings.style == bass::Style::Slap);
    CHECK(presets[3].settings.style == bass::Style::Mute);
}

TEST(basspresetstore, every_factory_preset_sounds_and_a_hard_chord_stays_below_full_scale)
{
    constexpr double rate = 48000.0;
    for (const auto& p : bass::factoryPresets()) {
        BassGuitar b;
        b.prepare(rate, 256);
        b.setSettings(p.settings);
        b.reset();
        MidiBuffer midi;
        for (int key : {28, 40, 47, 55}) midi.add({0, 0x90, std::uint8_t(key), 127});   // one on each string
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
            b.process(c);
            midi.clear();
            for (float x : l) { sum += double(x) * x; peak = std::max(peak, double(std::abs(x))); ++n; }
        }
        const double rms = std::sqrt(sum / double(n));
        if (!(rms > 0.005 && peak < 1.0)) std::printf("    %s: rms %.4f peak %.3f\n", p.name.c_str(), rms, peak);
        CHECK(rms > 0.005);
        CHECK(peak < 1.0);
    }
}

TEST(basspresetstore, preset_names_are_trimmed_and_checked)
{
    CHECK(bass::cleanPresetName("  My bass ") == std::optional<std::string>("My bass"));
    CHECK(!bass::cleanPresetName("   "));
    CHECK(!bass::cleanPresetName("a/b"));
    CHECK(!bass::cleanPresetName("a\\b"));
    CHECK(!bass::cleanPresetName("tab\there"));
    CHECK(!bass::cleanPresetName(std::string(49, 'x')));
    CHECK(bass::cleanPresetName(std::string(48, 'x')));
}

TEST(basspresetstore, preset_files_round_trip_and_bad_ones_are_refused)
{
    using namespace persistence::bassjson;
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
    { auto b = j; b["format"] = "ChaosMakerSynthPreset"; CHECK(refused(b)); }
    { auto b = j; b["version"] = 2; CHECK(refused(b)); CHECK(error.find("newer") != std::string::npos); }
    { auto b = j; b.erase("name"); CHECK(refused(b)); }
    { auto b = j; b["name"] = "a/b"; CHECK(refused(b)); }
    { auto b = j; b["settings"]["tone"] = -5; CHECK(refused(b)); }
    { auto b = j; b.erase("settings"); CHECK(refused(b)); }
}

TEST(basspresetstore, the_store_saves_renames_and_deletes_user_presets_but_never_factory_ones)
{
    TempFolder folder("store");
    BassPresetStore store(folder.path);
    std::string error;
    CHECK(store.all().size() == 6);                                 // the folder need not exist yet
    CHECK(store.userPresets().empty());

    CHECK(store.save(userPreset("Deep 1"), false, error));
    CHECK(fs::is_directory(folder.path));
    CHECK(fs::exists(folder.path / "deep-1.basspreset"));
    CHECK(store.userPresets().size() == 1 && store.all().size() == 7);
    CHECK(store.all().back().name == "Deep 1" && !store.all().back().factory);
    CHECK(!store.save(userPreset("deep 1"), false, error));         // names are unique ignoring case
    CHECK(error.find("already exists") != std::string::npos);
    CHECK(store.save(userPreset("DEEP 1", 0.75), true, error));     // replace when asked
    CHECK(store.userPresets().size() == 1);
    CHECK(store.find("deep 1") && store.find("deep 1")->settings.tone == 0.75);

    auto wild = userPreset("Wild");                                 // stored sanitized
    wild.settings.tone = 9.0;
    CHECK(store.save(wild, false, error));
    CHECK(store.find("Wild")->settings.tone == 1.0);

    CHECK(!store.save(userPreset("slap"), false, error));           // a factory name
    CHECK(!store.save(userPreset("Dub"), true, error));
    CHECK(!store.save(userPreset("  "), false, error));
    CHECK(!store.rename("Rock Drive", "Mine", error));
    CHECK(!store.remove("Palm Muted", error));
    CHECK(store.find("palm muted") && store.find("Palm Muted")->factory);

    CHECK(store.save(userPreset("Another"), false, error));
    const auto users = store.userPresets();                         // sorted by name
    CHECK(users.size() == 3 && users[0].name == "Another" && users[1].name == "DEEP 1" && users[2].name == "Wild");

    CHECK(!store.rename("Another", "Wild", error));                 // taken
    CHECK(!store.rename("Another", "Bright J-Bass", error));        // a factory name
    CHECK(store.rename("Another", "another", error));               // only the case changes
    CHECK(store.rename("another", "Third", error));
    CHECK(!store.find("Another") && store.find("Third"));
    CHECK(store.remove("third", error));
    CHECK(!store.find("Third") && store.userPresets().size() == 2);
    CHECK(!store.remove("Third", error));

    // A broken file or a file using a factory name is skipped, not fatal.
    { std::ofstream(folder.path / "broken.basspreset") << "{ not json"; }
    { std::ofstream(folder.path / "sneaky.basspreset") << persistence::bassjson::presetToJson(userPreset("Dub")).dump(); }
    { std::ofstream(folder.path / "other.txt") << "ignored"; }
    CHECK(store.userPresets().size() == 2);
    // A second store on the same folder sees the same presets (they live on disk).
    CHECK(BassPresetStore(folder.path).userPresets().size() == 2);
}

TEST(basspresetstore, a_store_without_a_folder_refuses_to_save)
{
    BassPresetStore store{fs::path()};
    std::string error;
    CHECK(store.all().size() == 6);
    CHECK(!store.save(userPreset("Mine"), false, error) && !error.empty());
}
