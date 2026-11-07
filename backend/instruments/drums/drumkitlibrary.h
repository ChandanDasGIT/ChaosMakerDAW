#pragma once
#include "instruments/drums/drumkitsettings.h"
#include "instruments/sampler/samplezone.h"
#include "instruments/sfz/sfzloader.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Sampler;

// The acoustic drum kit's samples, read once and shared by every Drums track.
//
// The kit (Gogodze Phu Vol II) is found in $CHAOSMAKER_SAMPLE_KITS, or else in a sample-kits
// folder next to the program (up to three folders up: build/appChaosMaker finds the
// project's sample-kits). Loading and every rebuild for new settings run on one background
// thread; finished zones go to the track's sampler with Sampler::stageZones().
namespace drumkit {

constexpr const char* KitFolderName = "AccousticDrums/Gogodze_Phu_vol_II";
constexpr const char* KitProgram = "Programs/Kit.sfz";

// Where the kit is looked for; empty when no sample-kits folder is found.
std::string findKitFolder();

// Level trim per room: more mics are louder, so each room sits near the same loudness with
// about 6 dB of headroom on a full groove.
double roomTrimDb(Room room) noexcept;

// The kit's zones for these settings: one pass per drummap::passes() entry, the keys moved to
// the drum map, the room trim, part levels and the volume added. Reads samples into `cache` as needed.
std::vector<SampleZone> buildZones(const sfz::Document& kit, const Settings& settings,
                                   const std::shared_ptr<sfz::SampleCache>& cache,
                                   sfz::LoadReport* report = nullptr);

class Library {
public:
    enum class State { Idle, Loading, Ready, Missing };

    explicit Library(std::string kitFolder);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    // The library every Drums track uses, made on first use with findKitFolder().
    static std::shared_ptr<Library> shared();

    // Builds zones for `settings` in the background and stages them on `target` if it still
    // exists. A newer request for the same target replaces one not started yet.
    void request(std::weak_ptr<Sampler> target, const Settings& settings);

    State state() const;
    std::string message() const;          // what the user should know: loading, or where the kit was looked for
    std::string kitFolder() const { return m_folder; }
    void waitUntilIdle();                 // tests: every request handled

private:
    struct Job {
        std::weak_ptr<Sampler> target;
        Settings settings;
    };
    void run();
    bool loadKit();                       // worker thread, once

    const std::string m_folder;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake, m_idle;
    std::deque<Job> m_jobs;
    bool m_busy = false, m_stop = false;
    State m_state = State::Idle;
    std::string m_message;
    bool m_loaded = false;
    sfz::Document m_kit;                  // worker thread only after loading
    std::shared_ptr<sfz::SampleCache> m_cache = std::make_shared<sfz::SampleCache>();
    std::thread m_worker;                 // last: started when everything above exists
};

} // namespace drumkit
