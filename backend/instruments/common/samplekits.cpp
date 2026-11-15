#include "samplekits.h"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

std::string samplekits::userFolder()
{
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data)
        return (fs::path(data) / "ChaosMaker/sample-kits").string();
    if (const char* home = std::getenv("HOME"); home && *home)
        return (fs::path(home) / ".local/share/ChaosMaker/sample-kits").string();
    return {};
}

std::string samplekits::folder()
{
    if (const char* env = std::getenv("CHAOSMAKER_SAMPLE_KITS"); env && *env) return env;
    std::error_code error;
    const fs::path exe = fs::read_symlink("/proc/self/exe", error);
    if (!error) {
        fs::path dir = exe.parent_path();
        for (int up = 0; up < 4 && !dir.empty(); ++up) {
            if (fs::is_directory(dir / "resources/sample-kits", error)) return (dir / "resources/sample-kits").string();
            if (dir == dir.root_path()) break;
            dir = dir.parent_path();
        }
    }
    const auto user = userFolder();
    return !user.empty() && fs::is_directory(user, error) ? user : std::string();
}
