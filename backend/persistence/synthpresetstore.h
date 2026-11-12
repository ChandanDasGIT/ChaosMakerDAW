#pragma once

#include "instruments/synth/synthpresets.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The synth presets a user can pick: the factory ones and the user's own, which live as
// .synthpreset files (synthjson) in one folder (the app's data folder; tests pass a temporary
// one). Names are unique ignoring case, across factory and user presets; factory presets cannot
// be replaced, renamed or deleted. Files that do not read are skipped.
class SynthPresetStore {
public:
    explicit SynthPresetStore(std::filesystem::path folder);

    const std::filesystem::path& folder() const noexcept { return m_folder; }

    // Factory presets first, in their own order, then the user's sorted by name.
    std::vector<synth::Preset> all() const;
    std::vector<synth::Preset> userPresets() const;
    std::optional<synth::Preset> find(std::string_view name) const;

    // Saves a user preset. A user preset of the same name is replaced only when `replace`;
    // a factory name is always refused. The settings are sanitized.
    bool save(synth::Preset preset, bool replace, std::string& error);
    bool rename(std::string_view name, std::string_view newName, std::string& error);
    bool remove(std::string_view name, std::string& error);

private:
    struct Entry { synth::Preset preset; std::filesystem::path file; };
    std::vector<Entry> entries() const;
    std::optional<Entry> userEntry(std::string_view name) const;
    static bool isFactoryName(std::string_view name);
    std::filesystem::path freeFileFor(std::string_view name) const;
    bool write(const synth::Preset& preset, const std::filesystem::path& file, std::string& error) const;

    std::filesystem::path m_folder;
};
