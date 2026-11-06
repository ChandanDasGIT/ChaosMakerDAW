#pragma once

#include "engine/enginesnapshot.h"
#include "processor/trackinput.h"
#include "session/project.h"
#include "dsp/trackmeters.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class SnapshotBuilder {
public:
    struct TrackEffects {
        std::shared_ptr<TrackMeters> meters = std::make_shared<TrackMeters>();
        std::shared_ptr<Processor> instrument;
        std::shared_ptr<TrackInput> input;
        std::shared_ptr<TrackInput> playbackInput;
        std::shared_ptr<Processor> chain;
        std::shared_ptr<Processor> playbackChain;
        int inputChannel = -1;
        std::string source;
    };

    struct RoutingResult {
        std::string error;
        int monitoredTrack = -1;
        bool cleanMonitorEnabled = false;
    };

    // Lightweight descriptor extracted on UI/caller thread for safe off-thread building
    struct Context {
        TempoMap tempo;
        float masterVolume = 1.0f;
        bool masterMuted = false;
        struct TrackInfo {
            int id = 0;
            TrackKind kind = TrackKind::Audio;
            std::string type;
            float volume = 1.0f;
            float pan = 0.0f;
            bool solo = false;
            bool muted = false;
            bool recordArmed = false;
            bool monitorEnabled = false;
            bool hasLiveInput = false;
            std::vector<AudioClip> audioClips;
        };
        std::vector<TrackInfo> tracks;

        static Context fromProject(const Project& project);
    };

    using AudioResolver = std::function<std::shared_ptr<const AudioData>(const std::string& poolId)>;
    using EffectResolver = std::function<const TrackEffects*(int trackId)>;
    using MutableEffectResolver = std::function<TrackEffects*(int trackId)>;
    using ChannelConnector = std::function<int(const std::string& source)>;
    using ErrorCallback = std::function<std::string()>;

    // Synchronize EQ bands from project tracks to TrackInput processors
    static void syncTrackEq(const Project& project, const MutableEffectResolver& effectLookup);
    static void syncTrackEq(const Project& project, std::unordered_map<int, TrackEffects>& effects);

    // Compute hardware input routing, capacity check, and channel connections
    static RoutingResult syncRouting(
        const Project& project,
        const MutableEffectResolver& effectLookup,
        const std::string& defaultInput,
        const std::vector<std::string>& availableInputPorts,
        int channelCapacity,
        const ChannelConnector& connector,
        const ErrorCallback& lastErrorFn = nullptr);

    static RoutingResult syncRouting(
        const Project& project,
        std::unordered_map<int, TrackEffects>& effects,
        const std::string& defaultInput,
        const std::vector<std::string>& availableInputPorts,
        int channelCapacity,
        const ChannelConnector& connector,
        const ErrorCallback& lastErrorFn = nullptr);

    // Build playback snapshot synchronously
    static std::unique_ptr<EngineSnapshot> buildPlaybackSnapshot(
        const Context& context,
        const AudioResolver& audioResolver,
        const EffectResolver& effectResolver = nullptr,
        bool multiMonitor = true);

    static std::unique_ptr<EngineSnapshot> buildPlaybackSnapshot(
        const Context& context,
        const AudioResolver& audioResolver,
        const std::unordered_map<int, TrackEffects>& effects,
        bool multiMonitor = true);

    static std::unique_ptr<EngineSnapshot> buildPlaybackSnapshot(
        const Project& project,
        const AudioResolver& audioResolver,
        const EffectResolver& effectResolver = nullptr,
        bool multiMonitor = true);

    static std::unique_ptr<EngineSnapshot> buildPlaybackSnapshot(
        const Project& project,
        const AudioResolver& audioResolver,
        const std::unordered_map<int, TrackEffects>& effects,
        bool multiMonitor = true);

    // Build playback snapshot asynchronously off-thread
    static std::future<std::unique_ptr<EngineSnapshot>> buildPlaybackSnapshotAsync(
        Context context,
        AudioResolver audioResolver,
        std::unordered_map<int, TrackEffects> effects,
        bool multiMonitor = true);

    // Build master chain snapshot
    static std::unique_ptr<EngineSnapshot> buildMasterChainSnapshot(std::shared_ptr<Processor> chain);

    // Build empty / reset snapshot (used for stopping / clearing)
    static std::unique_ptr<EngineSnapshot> buildEmptySnapshot();
};
