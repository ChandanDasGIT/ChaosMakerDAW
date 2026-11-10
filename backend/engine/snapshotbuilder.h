#pragma once

#include "engine/enginesnapshot.h"
#include "processor/trackinput.h"
#include "plugins/eq/tenbandeq.h"
#include "plugins/chaoseq/chaoseq.h"
#include "plugins/mixfx/mixeffect.h"
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
#include <map>
#include <vector>

class SnapshotBuilder {
public:
    struct TrackEffects {
        std::shared_ptr<TrackMeters> meters = std::make_shared<TrackMeters>();
        std::shared_ptr<Processor> instrument;
        std::shared_ptr<TrackInput> input;
        std::shared_ptr<TrackInput> playbackInput;
        // The track's EQ effect (monitor and playback instances); runs only when "eq" is on
        // the track's effect list, at its place in the list.
        std::shared_ptr<TenBandEq> eq;
        std::shared_ptr<TenBandEq> playbackEq;
        // Chaos EQ (plan 6.2), the same way: runs only when "chaoseq" is on the list.
        std::shared_ptr<ChaosEq> chaosEq;
        std::shared_ptr<ChaosEq> playbackChaosEq;
        // The mixing plugins (restMap G7) by type, the same way: each runs only when its type
        // is on the list. Made by ensureMixEffects() when a type first goes on the list (they
        // can hold seconds of audio), remade by remakeMixEffects(); settings by syncTrackEq.
        std::map<std::string, std::shared_ptr<MixEffect>, std::less<>> mixEffects;
        std::map<std::string, std::shared_ptr<MixEffect>, std::less<>> playbackMixEffects;
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
            automation::VolumeEnvelope volumeEnvelope;
            std::vector<std::string> effects; // effect types in processing order
        };
        std::vector<TrackInfo> tracks;
        std::vector<std::string> masterEffects;   // the master track's effect types, in order (G9)

        static Context fromProject(const Project& project);
    };

    using AudioResolver = std::function<std::shared_ptr<const AudioData>(const std::string& poolId)>;
    using EffectResolver = std::function<const TrackEffects*(int trackId)>;
    using MutableEffectResolver = std::function<TrackEffects*(int trackId)>;
    using ChannelConnector = std::function<int(const std::string& source)>;
    using ErrorCallback = std::function<std::string()>;

    // The processor that runs effect `type` for a track (the playback or the monitor instance);
    // null when the track has none for it.
    static std::shared_ptr<Processor> processorFor(const TrackEffects& effects, const std::string& type, bool playback);
    // A prepared monitor and playback instance of each mixing plugin in `types` that the track
    // does not have yet (UI thread); other types are ignored.
    static void ensureMixEffects(TrackEffects& effects, const std::vector<std::string>& types, double sampleRate, int maxFrames);
    // Fresh instances of the mixing plugins the track has (a new device: the old ones may still
    // be playing on the audio thread, so they are replaced, never prepared again).
    static void remakeMixEffects(TrackEffects& effects, double sampleRate, int maxFrames);
    // Copy each track's EQ settings (10-band and Chaos EQ) into its EQ processors
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
