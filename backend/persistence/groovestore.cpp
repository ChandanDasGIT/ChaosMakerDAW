#include "groovestore.h"

#include "drumgroovejson.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;
using drumgroove::Groove;

namespace {

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool sameName(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

std::optional<Groove> readFile(const fs::path& file)
{
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size == 0 || size > persistence::drumgroovejson::MaxFileSize) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded()) return std::nullopt;
    Groove g;
    std::string error;
    if (!persistence::drumgroovejson::fromJson(json, g, error)) return std::nullopt;
    return g;
}

} // namespace

GrooveStore::GrooveStore(fs::path folder) : m_folder(std::move(folder)) {}

std::vector<GrooveStore::Entry> GrooveStore::entries() const
{
    std::vector<Entry> out;
    std::error_code ec;
    if (m_folder.empty() || !fs::is_directory(m_folder, ec)) return out;
    for (const auto& item : fs::directory_iterator(m_folder, ec)) {
        if (!item.is_regular_file(ec) || item.path().extension() != ".groove") continue;
        auto groove = readFile(item.path());
        if (!groove || isFactoryName(groove->name)) continue;
        const bool duplicate = std::any_of(out.begin(), out.end(), [&](const Entry& e) { return sameName(e.groove.name, groove->name); });
        if (!duplicate) out.push_back({std::move(*groove), item.path()});
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.groove.barBeats() != b.groove.barBeats()) return a.groove.barBeats() < b.groove.barBeats();
        if (a.groove.denominator != b.groove.denominator) return a.groove.denominator < b.groove.denominator;
        return lower(a.groove.name) < lower(b.groove.name);
    });
    return out;
}

std::vector<Groove> GrooveStore::userGrooves() const
{
    std::vector<Groove> out;
    for (auto& e : entries()) out.push_back(std::move(e.groove));
    return out;
}

std::vector<Groove> GrooveStore::all() const
{
    std::vector<Groove> out = drumgroove::factoryGrooves();
    for (auto& g : userGrooves()) out.push_back(std::move(g));
    return out;
}

std::optional<Groove> GrooveStore::find(std::string_view name) const
{
    for (const auto& g : drumgroove::factoryGrooves()) if (sameName(g.name, name)) return g;
    if (auto e = userEntry(name)) return e->groove;
    return std::nullopt;
}

bool GrooveStore::isFactoryName(std::string_view name) const
{
    const auto& factory = drumgroove::factoryGrooves();
    return std::any_of(factory.begin(), factory.end(), [&](const Groove& g) { return sameName(g.name, name); });
}

std::optional<GrooveStore::Entry> GrooveStore::userEntry(std::string_view name) const
{
    for (auto& e : entries()) if (sameName(e.groove.name, name)) return e;
    return std::nullopt;
}

fs::path GrooveStore::freeFileFor(std::string_view name) const
{
    std::string stem;
    for (unsigned char c : name) {
        if (std::isalnum(c)) stem += static_cast<char>(std::tolower(c));
        else if ((c == ' ' || c == '-' || c == '_') && !stem.empty() && stem.back() != '-') stem += '-';
    }
    while (!stem.empty() && stem.back() == '-') stem.pop_back();
    if (stem.empty()) stem = "groove";
    std::error_code ec;
    fs::path file = m_folder / (stem + ".groove");
    for (int n = 2; fs::exists(file, ec); ++n) file = m_folder / (stem + "-" + std::to_string(n) + ".groove");
    return file;
}

bool GrooveStore::write(const Groove& groove, const fs::path& file, std::string& error) const
{
    std::error_code ec;
    fs::create_directories(m_folder, ec);
    if (ec) { error = "Could not create the grooves folder."; return false; }
    const fs::path temp = fs::path(file).concat(".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Could not write the groove file."; return false; }
        out << persistence::drumgroovejson::toJson(groove).dump(2) << '\n';
        if (!out) { error = "Could not write the groove file."; fs::remove(temp, ec); return false; }
    }
    fs::rename(temp, file, ec);
    if (ec) { error = "Could not write the groove file."; fs::remove(temp, ec); return false; }
    return true;
}

bool GrooveStore::save(Groove groove, bool replace, std::string& error)
{
    const auto clean = drumgroove::cleanName(groove.name);
    if (!clean) { error = "Give the groove a name (up to 48 characters, no slashes)."; return false; }
    groove.name = *clean;
    groove.factory = false;
    if (m_folder.empty()) { error = "There is no folder for your grooves."; return false; }
    if (isFactoryName(groove.name)) { error = "\"" + groove.name + "\" is a built-in groove; choose another name."; return false; }
    if (groove.notes.empty() || !drumgroove::validMeter(groove.numerator, groove.denominator)
        || groove.bars < 1 || groove.bars > drumgroove::MaxBars) {
        error = "This groove cannot be saved.";
        return false;
    }
    const auto existing = userEntry(groove.name);
    if (existing && !replace) { error = "A groove named \"" + existing->groove.name + "\" already exists."; return false; }
    return write(groove, existing ? existing->file : freeFileFor(groove.name), error);
}

bool GrooveStore::rename(std::string_view name, std::string_view newName, std::string& error)
{
    if (isFactoryName(name)) { error = "Built-in grooves cannot be renamed."; return false; }
    auto entry = userEntry(name);
    if (!entry) { error = "There is no groove of that name."; return false; }
    const auto clean = drumgroove::cleanName(newName);
    if (!clean) { error = "Give the groove a name (up to 48 characters, no slashes)."; return false; }
    if (isFactoryName(*clean)) { error = "\"" + *clean + "\" is a built-in groove; choose another name."; return false; }
    if (!sameName(*clean, entry->groove.name) && userEntry(*clean)) {
        error = "A groove named \"" + *clean + "\" already exists.";
        return false;
    }
    entry->groove.name = *clean;
    return write(entry->groove, entry->file, error);
}

bool GrooveStore::remove(std::string_view name, std::string& error)
{
    if (isFactoryName(name)) { error = "Built-in grooves cannot be deleted."; return false; }
    const auto entry = userEntry(name);
    if (!entry) { error = "There is no groove of that name."; return false; }
    std::error_code ec;
    if (!fs::remove(entry->file, ec) || ec) { error = "Could not delete the groove file."; return false; }
    return true;
}
