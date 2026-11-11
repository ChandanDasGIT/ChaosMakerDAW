#include "persistence/mediacollect.h"

#include <fstream>
#include <set>

namespace persistence {
namespace fs = std::filesystem;

namespace {
bool inside(const fs::path& file, const fs::path& folder)
{
    const auto rel = file.lexically_relative(folder);
    return !rel.empty() && *rel.begin() != "..";
}

// The same bytes (an earlier save's copy of this file).
bool sameContent(const fs::path& a, const fs::path& b)
{
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    if (!x || !y) return false;
    std::vector<char> bx(1 << 16), by(1 << 16);
    while (x && y) {
        x.read(bx.data(), std::streamsize(bx.size()));
        y.read(by.data(), std::streamsize(by.size()));
        if (x.gcount() != y.gcount() || !std::equal(bx.begin(), bx.begin() + x.gcount(), by.begin())) return false;
    }
    return x.eof() && y.eof();
}
}

MediaCollection collectMedia(const Project& project, const TrackList::MediaPaths& media, const fs::path& projectFile)
{
    MediaCollection result;
    std::error_code ec;
    const fs::path projectDir = fs::weakly_canonical(fs::absolute(projectFile, ec).parent_path(), ec);
    if (projectDir.empty()) return result;
    const fs::path folder = projectDir / (projectFile.stem().string() + " Media");

    std::set<std::string> files;   // each file once, in a stable order
    for (const auto& track : project.tracks())
        for (const auto& clip : track->audioClips)
            if (!clip.poolId.empty() && media.contains(clip.poolId)) files.insert(clip.poolId);

    std::set<fs::path> taken;      // copies made in this save (two sources with one name)
    for (const auto& source : files) {
        const fs::path from = fs::weakly_canonical(fs::path(source), ec);
        if (ec || !fs::is_regular_file(from, ec)) continue;   // gone: the load says so later
        if (inside(from, projectDir)) continue;               // already travels with the project
        const auto size = fs::file_size(from, ec);
        if (ec) { result.problems.push_back(source + ": " + ec.message()); continue; }

        // The same name; a different file with that name gets "-2", "-3" ...
        const fs::path stem = from.stem(), ext = from.extension();
        fs::path to;
        bool reuse = false;
        for (int n = 1; n < 10000; ++n) {
            const fs::path candidate = folder / (n == 1 ? from.filename() : fs::path(stem.string() + "-" + std::to_string(n) + ext.string()));
            if (taken.contains(candidate)) continue;
            std::error_code e;
            if (!fs::exists(candidate, e)) { to = candidate; break; }
            if (fs::is_regular_file(candidate, e) && fs::file_size(candidate, e) == size && !e && sameContent(from, candidate)) { to = candidate; reuse = true; break; }
        }
        if (to.empty()) { result.problems.push_back(source + ": no free name in " + folder.string()); continue; }
        if (!reuse) {
            fs::create_directories(folder, ec);
            if (ec) { result.problems.push_back(source + ": cannot make " + folder.string() + " (" + ec.message() + ")"); ec.clear(); continue; }
            // Through a temporary name, so a failed copy never looks like a finished one.
            fs::path partial = to;
            partial += ".part";
            fs::copy_file(from, partial, fs::copy_options::overwrite_existing, ec);
            if (!ec) fs::rename(partial, to, ec);
            if (ec) {
                result.problems.push_back(source + ": " + ec.message());
                std::error_code ignore;
                fs::remove(partial, ignore);
                ec.clear();
                continue;
            }
        }
        taken.insert(to);
        result.paths[source] = to.string();
    }
    return result;
}

} // namespace persistence
