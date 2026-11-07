#include "instruments/drums/drumkitlibrary.h"

#include "instruments/drums/drummap.h"
#include "instruments/sampler/sampler.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace drumkit {

namespace fs = std::filesystem;

std::string findKitFolder()
{
    if (const char* env = std::getenv("CHAOSMAKER_SAMPLE_KITS"); env && *env)
        return (fs::path(env) / KitFolderName).string();
    std::error_code error;
    const fs::path exe = fs::read_symlink("/proc/self/exe", error);
    if (error) return {};
    fs::path dir = exe.parent_path();
    for (int up = 0; up < 4 && !dir.empty(); ++up) {
        if (fs::is_directory(dir / "sample-kits", error)) return (dir / "sample-kits" / KitFolderName).string();
        if (dir == dir.root_path()) break;
        dir = dir.parent_path();
    }
    return {};
}

namespace {

// The part a drum-map key belongs to, for the part levels.
double partLevelDb(int key, const Settings& s)
{
    if (key == 60) return s.kickDb;
    if (key == 61 || key == 62 || key == 64) return s.snareDb;
    if (key == 65 || key == 67 || key == 69) return s.tomsDb;
    return s.hihatDb;                                       // 55-57, 66, 68, 70
}

} // namespace

double roomTrimDb(Room room) noexcept
{
    switch (room) {
    case Room::Close: return -3.0;
    case Room::Full: return -11.0;
    default: return -6.0;
    }
}

std::vector<SampleZone> buildZones(const sfz::Document& kit, const Settings& in,
                                   const std::shared_ptr<sfz::SampleCache>& cache, sfz::LoadReport* report)
{
    const Settings settings = sanitize(in);
    std::vector<SampleZone> zones;
    for (const auto& pass : drummap::passes()) {
        sfz::LoadOptions options;
        options.controllers = controllers(settings);
        options.controllers[4] = pass.hihatPedal;
        options.cache = cache;
        auto result = sfz::buildZones(kit, options);
        if (report && pass.fromKey < 0) *report = result.report;   // the full pass speaks for the kit
        for (auto& zone : result.zones) {
            const auto covers = [&zone](int key) { return zone.loKey <= key && key <= zone.hiKey; };
            if (pass.fromKey < 0) {
                // The kit's hi-hat keys are played by the other passes, on keys of their own.
                if (covers(drummap::KitHihatKey) || covers(drummap::KitTightHihatKey)) continue;
                zone.loKey += drummap::KitOffset;
                zone.hiKey += drummap::KitOffset;
                zone.rootKey += drummap::KitOffset;
            } else {
                if (zone.loKey != pass.fromKey || zone.hiKey != pass.fromKey) continue;
                zone.rootKey += pass.toKey - pass.fromKey;
                zone.loKey = zone.hiKey = pass.toKey;
            }
            zones.push_back(std::move(zone));
        }
    }
    for (auto& zone : zones)
        zone.volumeDb += float(roomTrimDb(settings.room) + partLevelDb(zone.loKey, settings) + settings.volumeDb);
    return zones;
}

Library::Library(std::string kitFolder) : m_folder(std::move(kitFolder)), m_worker([this] { run(); }) {}

Library::~Library()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

std::shared_ptr<Library> Library::shared()
{
    static std::mutex mutex;
    static std::shared_ptr<Library> library;
    std::lock_guard lock(mutex);
    if (!library) library = std::make_shared<Library>(findKitFolder());
    return library;
}

void Library::request(std::weak_ptr<Sampler> target, const Settings& settings)
{
    {
        std::lock_guard lock(m_mutex);
        const auto same = [&target](const Job& job) {
            return !job.target.owner_before(target) && !target.owner_before(job.target);
        };
        std::erase_if(m_jobs, same);
        m_jobs.push_back({std::move(target), settings});
    }
    m_wake.notify_all();
}

Library::State Library::state() const
{
    std::lock_guard lock(m_mutex);
    return m_state;
}

std::string Library::message() const
{
    std::lock_guard lock(m_mutex);
    return m_message;
}

void Library::waitUntilIdle()
{
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_jobs.empty() && !m_busy; });
}

bool Library::loadKit()
{
    const fs::path program = fs::path(m_folder) / KitProgram;
    std::error_code error;
    if (m_folder.empty() || !fs::is_regular_file(program, error)) {
        std::lock_guard lock(m_mutex);
        m_state = State::Missing;
        m_message = m_folder.empty()
            ? "Drum kit not found: put Gogodze Phu Vol II in sample-kits/" + std::string(KitFolderName)
            : "Drum kit not found at " + program.string();
        return false;
    }
    {
        std::lock_guard lock(m_mutex);
        m_state = State::Loading;
        m_message = "Loading the drum kit...";
    }
    m_kit = sfz::parseFile(program.string());
    m_loaded = true;
    return true;
}

void Library::run()
{
    for (;;) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
            if (m_stop) return;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
            m_busy = true;
        }
        if ((m_loaded || loadKit()) && !job.target.expired()) {
            auto zones = buildZones(m_kit, job.settings, m_cache);
            if (auto sampler = job.target.lock()) {
                sampler->stageZones(std::move(zones));
                sampler->collectRetired();
            }
            std::lock_guard lock(m_mutex);
            m_state = State::Ready;
            m_message.clear();
        }
        {
            std::lock_guard lock(m_mutex);
            m_busy = false;
        }
        m_idle.notify_all();
    }
}

} // namespace drumkit
