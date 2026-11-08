#include "instruments/piano/pianolibrary.h"

#include "instruments/sampler/sampler.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace piano {

namespace fs = std::filesystem;

std::string findFolder()
{
    if (const char* env = std::getenv("CHAOSMAKER_SAMPLE_KITS"); env && *env)
        return (fs::path(env) / FolderName).string();
    std::error_code error;
    const fs::path exe = fs::read_symlink("/proc/self/exe", error);
    if (error) return {};
    fs::path dir = exe.parent_path();
    for (int up = 0; up < 4 && !dir.empty(); ++up) {
        if (fs::is_directory(dir / "sample-kits", error)) return (dir / "sample-kits" / FolderName).string();
        if (dir == dir.root_path()) break;
        dir = dir.parent_path();
    }
    return {};
}

std::vector<SampleZone> buildZones(const sfz::Document& map, const Settings& in,
                                   const std::shared_ptr<sfz::SampleCache>& cache, sfz::LoadReport* report)
{
    const Settings settings = sanitize(in);
    sfz::LoadOptions options;
    options.controllers = controllers(settings);
    options.liveControllers = {64};                         // the sustain pedal
    options.cache = cache;
    auto result = sfz::buildZones(map, options);
    if (report) *report = result.report;

    const double cutoff = brightnessCutoff(settings.brightness);
    std::vector<SampleZone> zones;
    zones.reserve(result.zones.size());
    for (auto& zone : result.zones) {
        // The scale changer: key k plays the zone of k + transpose, at that pitch.
        zone.loKey -= settings.transpose;
        zone.hiKey -= settings.transpose;
        zone.rootKey -= settings.transpose;
        if (zone.hiKey < 0 || zone.loKey > 127) continue;
        zone.loKey = std::max(zone.loKey, 0);
        zone.hiKey = std::min(zone.hiKey, 127);
        if (cutoff > 0.0)
            zone.cutoffHz = zone.cutoffHz > 0.0f ? std::min(zone.cutoffHz, float(cutoff)) : float(cutoff);
        zone.volumeDb += float(TrimDb + settings.volumeDb);
        zones.push_back(std::move(zone));
    }
    return zones;
}

Library::Library(std::string folder) : m_folder(std::move(folder)), m_worker([this] { run(); }) {}

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
    if (!library) library = std::make_shared<Library>(findFolder());
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

bool Library::loadMap()
{
    const fs::path program = fs::path(m_folder) / Program;
    std::error_code error;
    if (m_folder.empty() || !fs::is_regular_file(program, error)) {
        std::lock_guard lock(m_mutex);
        m_state = State::Missing;
        m_message = m_folder.empty()
            ? "Piano not found: put Splendid Grand Piano in sample-kits/" + std::string(FolderName)
            : "Piano not found at " + program.string();
        return false;
    }
    {
        std::lock_guard lock(m_mutex);
        m_state = State::Loading;
        m_message = "Loading the piano...";
    }
    m_map = sfz::parseFile(program.string());
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
        if ((m_loaded || loadMap()) && !job.target.expired()) {
            auto zones = buildZones(m_map, job.settings, m_cache);
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

} // namespace piano
