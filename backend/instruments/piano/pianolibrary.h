#pragma once
#include "instruments/piano/pianosettings.h"
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

// The grand piano's samples (Splendid Grand Piano), read once and shared by every Piano track.
// Decoded they take about 515 MB, loaded the first time a Piano track needs them.
//
// The library is found in $CHAOSMAKER_SAMPLE_KITS, or else in a sample-kits folder next to the
// program (up to three folders up), as the drum kit's. Loading and every rebuild for new
// settings run on one background thread; finished zones go to the track's sampler with
// Sampler::stageZones(). The sustain pedal (CC 64) stays live: the sampler follows it.
namespace piano {

constexpr const char* FolderName = "GrandPiano/SplendidGrandPiano";
constexpr const char* Program = "Splendid Grand Piano.sfz";

// Level trim on every zone: a forte chord of five notes peaks near 1.4 as recorded, so the
// piano sits 6 dB lower and leaves headroom (Settings::volumeDb is added on top).
constexpr double TrimDb = -6.0;

// Where the piano is looked for; empty when no sample-kits folder is found.
std::string findFolder();

// The piano's zones for these settings: the map's controllers, the scale changer (a played
// key sounds `transpose` semitones away), brightness, the trim and volume. Reads samples
// into `cache`.
std::vector<SampleZone> buildZones(const sfz::Document& map, const Settings& settings,
                                   const std::shared_ptr<sfz::SampleCache>& cache,
                                   sfz::LoadReport* report = nullptr);

class Library {
public:
    enum class State { Idle, Loading, Ready, Missing };

    explicit Library(std::string folder);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    // The library every Piano track uses, made on first use with findFolder().
    static std::shared_ptr<Library> shared();

    // Builds zones for `settings` in the background and stages them on `target` if it still
    // exists. A newer request for the same target replaces one not started yet.
    void request(std::weak_ptr<Sampler> target, const Settings& settings);

    State state() const;
    std::string message() const;          // loading, or where the piano was looked for
    std::string folder() const { return m_folder; }
    void waitUntilIdle();                 // tests: every request handled

private:
    struct Job {
        std::weak_ptr<Sampler> target;
        Settings settings;
    };
    void run();
    bool loadMap();                       // worker thread, once

    const std::string m_folder;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake, m_idle;
    std::deque<Job> m_jobs;
    bool m_busy = false, m_stop = false;
    State m_state = State::Idle;
    std::string m_message;
    bool m_loaded = false;
    sfz::Document m_map;                  // worker thread only after loading
    std::shared_ptr<sfz::SampleCache> m_cache = std::make_shared<sfz::SampleCache>();
    std::thread m_worker;                 // last: started when everything above exists
};

} // namespace piano
