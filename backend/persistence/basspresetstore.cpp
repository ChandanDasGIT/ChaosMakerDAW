#include "basspresetstore.h"

#include "bassjson.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;
using bass::Preset;

namespace {

constexpr const char* Extension = ".basspreset";
const std::string BadName = "Give the preset a name (up to 48 characters, no slashes).";

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool sameName(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

std::optional<Preset> readFile(const fs::path& file)
{
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size == 0 || size > persistence::bassjson::MaxPresetFileSize) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded()) return std::nullopt;
    Preset p;
    std::string error;
    if (!persistence::bassjson::presetFromJson(json, p, error)) return std::nullopt;
    return p;
}

} // namespace

BassPresetStore::BassPresetStore(fs::path folder) : m_folder(std::move(folder)) {}

std::vector<BassPresetStore::Entry> BassPresetStore::entries() const
{
    std::vector<Entry> out;
    std::error_code ec;
    if (m_folder.empty() || !fs::is_directory(m_folder, ec)) return out;
    for (const auto& item : fs::directory_iterator(m_folder, ec)) {
        if (!item.is_regular_file(ec) || item.path().extension() != Extension) continue;
        auto preset = readFile(item.path());
        if (!preset || isFactoryName(preset->name)) continue;
        const bool duplicate = std::any_of(out.begin(), out.end(), [&](const Entry& e) { return sameName(e.preset.name, preset->name); });
        if (!duplicate) out.push_back({std::move(*preset), item.path()});
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return lower(a.preset.name) < lower(b.preset.name); });
    return out;
}

std::vector<Preset> BassPresetStore::userPresets() const
{
    std::vector<Preset> out;
    for (auto& e : entries()) out.push_back(std::move(e.preset));
    return out;
}

std::vector<Preset> BassPresetStore::all() const
{
    std::vector<Preset> out = bass::factoryPresets();
    for (auto& p : userPresets()) out.push_back(std::move(p));
    return out;
}

std::optional<Preset> BassPresetStore::find(std::string_view name) const
{
    for (const auto& p : bass::factoryPresets()) if (sameName(p.name, name)) return p;
    if (auto e = userEntry(name)) return e->preset;
    return std::nullopt;
}

bool BassPresetStore::isFactoryName(std::string_view name)
{
    const auto& factory = bass::factoryPresets();
    return std::any_of(factory.begin(), factory.end(), [&](const Preset& p) { return sameName(p.name, name); });
}

std::optional<BassPresetStore::Entry> BassPresetStore::userEntry(std::string_view name) const
{
    for (auto& e : entries()) if (sameName(e.preset.name, name)) return e;
    return std::nullopt;
}

fs::path BassPresetStore::freeFileFor(std::string_view name) const
{
    std::string stem;
    for (unsigned char c : name) {
        if (std::isalnum(c)) stem += static_cast<char>(std::tolower(c));
        else if ((c == ' ' || c == '-' || c == '_') && !stem.empty() && stem.back() != '-') stem += '-';
    }
    while (!stem.empty() && stem.back() == '-') stem.pop_back();
    if (stem.empty()) stem = "preset";
    std::error_code ec;
    fs::path file = m_folder / (stem + Extension);
    for (int n = 2; fs::exists(file, ec); ++n) file = m_folder / (stem + "-" + std::to_string(n) + Extension);
    return file;
}

bool BassPresetStore::write(const Preset& preset, const fs::path& file, std::string& error) const
{
    std::error_code ec;
    fs::create_directories(m_folder, ec);
    if (ec) { error = "Could not create the bass presets folder."; return false; }
    const fs::path temp = fs::path(file).concat(".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Could not write the preset file."; return false; }
        out << persistence::bassjson::presetToJson(preset).dump(2) << '\n';
        if (!out) { error = "Could not write the preset file."; fs::remove(temp, ec); return false; }
    }
    fs::rename(temp, file, ec);
    if (ec) { error = "Could not write the preset file."; fs::remove(temp, ec); return false; }
    return true;
}

bool BassPresetStore::save(Preset preset, bool replace, std::string& error)
{
    const auto clean = bass::cleanPresetName(preset.name);
    if (!clean) { error = BadName; return false; }
    preset.name = *clean;
    preset.factory = false;
    preset.settings = bass::sanitize(preset.settings);
    if (m_folder.empty()) { error = "There is no folder for your bass presets."; return false; }
    if (isFactoryName(preset.name)) { error = "\"" + preset.name + "\" is a built-in preset; choose another name."; return false; }
    const auto existing = userEntry(preset.name);
    if (existing && !replace) { error = "A preset named \"" + existing->preset.name + "\" already exists."; return false; }
    return write(preset, existing ? existing->file : freeFileFor(preset.name), error);
}

bool BassPresetStore::rename(std::string_view name, std::string_view newName, std::string& error)
{
    if (isFactoryName(name)) { error = "Built-in presets cannot be renamed."; return false; }
    auto entry = userEntry(name);
    if (!entry) { error = "There is no preset of that name."; return false; }
    const auto clean = bass::cleanPresetName(newName);
    if (!clean) { error = BadName; return false; }
    if (isFactoryName(*clean)) { error = "\"" + *clean + "\" is a built-in preset; choose another name."; return false; }
    if (!sameName(*clean, entry->preset.name) && userEntry(*clean)) {
        error = "A preset named \"" + *clean + "\" already exists.";
        return false;
    }
    entry->preset.name = *clean;
    return write(entry->preset, entry->file, error);
}

bool BassPresetStore::remove(std::string_view name, std::string& error)
{
    if (isFactoryName(name)) { error = "Built-in presets cannot be deleted."; return false; }
    const auto entry = userEntry(name);
    if (!entry) { error = "There is no preset of that name."; return false; }
    std::error_code ec;
    if (!fs::remove(entry->file, ec) || ec) { error = "Could not delete the preset file."; return false; }
    return true;
}
