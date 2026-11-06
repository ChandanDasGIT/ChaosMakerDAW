#pragma once

#include <cstddef>
#include <string>
#include <vector>

// The "Open Recent" list: project file paths, newest first, no duplicates, at most
// maxEntries. Storage is up to the caller (the wrapper keeps it in the app settings).
class RecentProjects {
public:
    static constexpr std::size_t maxEntries = 10;

    const std::vector<std::string>& paths() const noexcept { return m_paths; }
    bool empty() const noexcept { return m_paths.empty(); }

    // Put a path first (moving it up if it is already listed). Empty paths are ignored.
    void add(const std::string& path);
    bool remove(const std::string& path);
    void clear() noexcept { m_paths.clear(); }

    // Replace the list, e.g. with stored values: keeps the order, drops empty paths and
    // duplicates, and keeps at most maxEntries.
    void setPaths(const std::vector<std::string>& paths);

    // Drop files that no longer exist. Returns how many were removed.
    std::size_t removeMissing();

    // Menu labels, one per path: the file name, plus its folder when two listed files
    // share a name ("song.chaos — demos").
    std::vector<std::string> labels() const;

private:
    std::vector<std::string> m_paths;
};
