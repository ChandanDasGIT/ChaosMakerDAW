#pragma once

#include "instruments/drums/grooves/drumgroove.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The drum grooves a user can pick: the factory ones and the user's own, which live as
// .groove files (drumgroovejson) in one folder (the app's data folder; tests pass a temporary
// one). Names are unique ignoring case, across factory and user grooves; factory grooves
// cannot be replaced, renamed or deleted. Files that do not read are skipped.
class GrooveStore {
public:
    explicit GrooveStore(std::filesystem::path folder);

    const std::filesystem::path& folder() const noexcept { return m_folder; }

    // Factory grooves first (3/4, 4/4, 7/8), then the user's sorted by meter and name.
    std::vector<drumgroove::Groove> all() const;
    std::vector<drumgroove::Groove> userGrooves() const;
    std::optional<drumgroove::Groove> find(std::string_view name) const;

    // Saves a user groove. A user groove of the same name is replaced only when `replace`;
    // a factory name is always refused.
    bool save(drumgroove::Groove groove, bool replace, std::string& error);
    bool rename(std::string_view name, std::string_view newName, std::string& error);
    bool remove(std::string_view name, std::string& error);

private:
    struct Entry { drumgroove::Groove groove; std::filesystem::path file; };
    std::vector<Entry> entries() const;
    std::optional<Entry> userEntry(std::string_view name) const;
    bool isFactoryName(std::string_view name) const;
    std::filesystem::path freeFileFor(std::string_view name) const;
    bool write(const drumgroove::Groove& groove, const std::filesystem::path& file, std::string& error) const;

    std::filesystem::path m_folder;
};
