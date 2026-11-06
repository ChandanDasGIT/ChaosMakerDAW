#include "session/recentprojects.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace {

std::string fileName(const std::string& path)
{
    return std::filesystem::path(path).filename().string();
}

std::string folderName(const std::string& path)
{
    return std::filesystem::path(path).parent_path().filename().string();
}

} // namespace

void RecentProjects::add(const std::string& path)
{
    if (path.empty()) return;
    remove(path);
    m_paths.insert(m_paths.begin(), path);
    if (m_paths.size() > maxEntries) m_paths.resize(maxEntries);
}

bool RecentProjects::remove(const std::string& path)
{
    const auto it = std::find(m_paths.begin(), m_paths.end(), path);
    if (it == m_paths.end()) return false;
    m_paths.erase(it);
    return true;
}

void RecentProjects::setPaths(const std::vector<std::string>& paths)
{
    m_paths.clear();
    for (const auto& path : paths) {
        if (m_paths.size() == maxEntries) break;
        if (path.empty() || std::find(m_paths.begin(), m_paths.end(), path) != m_paths.end()) continue;
        m_paths.push_back(path);
    }
}

std::size_t RecentProjects::removeMissing()
{
    const auto before = m_paths.size();
    std::erase_if(m_paths, [](const std::string& path) {
        std::error_code error;
        return !std::filesystem::is_regular_file(path, error);
    });
    return before - m_paths.size();
}

std::vector<std::string> RecentProjects::labels() const
{
    std::vector<std::string> result;
    result.reserve(m_paths.size());
    for (const auto& path : m_paths) {
        const std::string name = fileName(path);
        const bool shared = std::count_if(m_paths.begin(), m_paths.end(),
                                          [&](const std::string& other) { return fileName(other) == name; }) > 1;
        const std::string folder = folderName(path);
        result.push_back(shared && !folder.empty() ? name + " \xe2\x80\x94 " + folder : name);
    }
    return result;
}
