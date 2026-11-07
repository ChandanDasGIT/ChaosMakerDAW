#pragma once
#include "instruments/sampler/samplezone.h"
#include "instruments/sfz/sfzparser.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Turns an SFZ file into the sampler's zones. Runs off the audio thread.
//
// MIDI controllers are taken at the file's own defaults (<control> set_ccN / set_hdccN, others
// 0): a region whose locc/hicc range leaves them out is not loaded, and CC modulations of
// level, pan, tuning and the envelope are worked out once. CC 133 (the key) uses each region's
// root key. Live controller changes are not followed yet (the drums and the piano add them).
//
// Each sample file is read once, however many regions use it. Nothing stops a load: what
// cannot be used is left out and written in the report.
namespace sfz {

enum class SampleMode {
    Load,                                 // decode and keep: zones are ready for the sampler
    Verify,                               // decode to check, then let go (zones get no audio)
    Exists,                               // only check the files are there (zones get no audio)
};

// Samples read so far, by full path. Give one cache to several loads of the same file (other
// controller values) and each sample is read once. Safe to share between threads.
struct SampleCache {
    struct Entry {
        bool usable = false;              // read (or, when only checking, found)
        std::shared_ptr<const AudioData> audio;
        bool hasLoop = false;
        std::int64_t loopStart = 0, loopEnd = 0;
    };
    std::mutex mutex;
    std::map<std::string, Entry> entries;
};

struct LoadOptions {
    SampleMode samples = SampleMode::Load;
    std::map<int, double> controllers;    // CC number -> 0..127, over the file's own defaults
    std::shared_ptr<SampleCache> cache;   // none: a cache for this load only
};

struct ZoneSource {
    std::string sample;                   // full path of the sample file
    std::string file;                     // where the region is written
    int line = 0;
};

struct LoadReport {
    std::vector<Diagnostic> problems;     // the parser's, then the loader's, with file and line
    std::vector<std::string> missingSamples;
    std::map<std::string, std::string> unreadableSamples;   // path -> why (FLAC until C4, bad WAV)
    std::map<std::string, int> unusedOpcodes;    // understood, not played yet -> regions using it
    std::map<std::string, int> unknownOpcodes;   // not SFZ we know -> regions using it
    int regions = 0;                      // regions in the file
    int leftOutByControllers = 0;         // locc/hicc exclude the default CC values
    int leftOutOther = 0;                 // no or unusable sample, no key, silent, other trigger
    int samplesRead = 0;                  // distinct sample files decoded
    double sampleMegabytes = 0.0;         // their size in memory as float
};

struct LoadResult {
    std::vector<SampleZone> zones;
    std::vector<ZoneSource> sources;      // one per zone, same order
    LoadReport report;
};

LoadResult loadSfz(const std::string& path, const LoadOptions& options = {});
LoadResult buildZones(const Document& document, const LoadOptions& options = {});

// "c4" = 60, "C#4", "db-1", "021"; -1 when it is neither a note name nor a number in 0..127.
int parseKey(std::string_view text) noexcept;

} // namespace sfz
