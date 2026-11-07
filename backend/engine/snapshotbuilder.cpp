#include "engine/snapshotbuilder.h"
#include "instruments/harmonium/harmonium.h"
#include "dsp/panlaw.h"

SnapshotBuilder::Context SnapshotBuilder::Context::fromProject(const Project& project)
{
    Context ctx;
    ctx.tempo = project.tempo();
    ctx.masterVolume = project.masterVolume();
    ctx.masterMuted = project.masterMuted();
    ctx.tracks.reserve(project.tracks().size());
    for (const auto& track : project.tracks()) {
        TrackInfo info;
        info.id = track->id;
        info.kind = track->kind;
        info.type = track->type;
        info.volume = track->volume;
        info.pan = track->pan;
        info.solo = track->solo;
        info.muted = track->muted;
        info.recordArmed = track->recordArmed;
        info.monitorEnabled = track->monitorEnabled;
        info.hasLiveInput = track->hasLiveInput();
        info.audioClips = track->audioClips;
        for (const auto& slot : track->effects) info.effects.push_back(slot.type);
        ctx.tracks.push_back(std::move(info));
    }
    return ctx;
}

void SnapshotBuilder::syncTrackEq(const Project& project, const MutableEffectResolver& effectLookup)
{
    if (!effectLookup) return;
    for (const auto& track : project.tracks()) {
        auto* fx = effectLookup(track->id);
        if (!fx) continue;
        for (int band = 0; band < 10; ++band) {
            if (fx->eq) fx->eq->setBandGain(band, track->eqGainDb[band]);
            if (fx->playbackEq) fx->playbackEq->setBandGain(band, track->eqGainDb[band]);
        }
        if (fx->chaosEq) fx->chaosEq->setSettings(track->chaosEq);
        if (fx->playbackChaosEq) fx->playbackChaosEq->setSettings(track->chaosEq);
        if (fx->instrument && fx->instrument->typeId() == "harmonium")
            static_cast<Harmonium&>(*fx->instrument).setSettings(track->harmonium);
    }
}

void SnapshotBuilder::syncTrackEq(const Project& project, std::unordered_map<int, TrackEffects>& effects)
{
    syncTrackEq(project, [&effects](int id) -> TrackEffects* {
        auto it = effects.find(id);
        return it != effects.end() ? &it->second : nullptr;
    });
}

SnapshotBuilder::RoutingResult SnapshotBuilder::syncRouting(
    const Project& project,
    const MutableEffectResolver& effectLookup,
    const std::string& defaultInput,
    const std::vector<std::string>& availableInputPorts,
    int channelCapacity,
    const ChannelConnector& connector,
    const ErrorCallback& lastErrorFn)
{
    RoutingResult result;
    std::unordered_set<std::string> sources;
    for (const auto& track : project.tracks()) {
        if (track->hasLiveInput() && (track->recordArmed || track->monitorEnabled)) {
            sources.insert(track->inputPort.empty() ? defaultInput : track->inputPort);
        }
    }

    const bool capacity = static_cast<int>(sources.size()) <= channelCapacity;
    if (!capacity) {
        result.error = "The audio device cannot route all selected track inputs independently.";
    }

    auto portAvailable = [&](const std::string& port) {
        return std::find(availableInputPorts.begin(), availableInputPorts.end(), port) != availableInputPorts.end();
    };

    result.monitoredTrack = -1;
    for (const auto& track : project.tracks()) {
        auto* fx = effectLookup ? effectLookup(track->id) : nullptr;
        if (!fx) continue;
        if (!track->hasLiveInput() || (!track->recordArmed && !track->monitorEnabled)) continue;

        const std::string source = track->inputPort.empty() ? defaultInput : track->inputPort;
        if (!capacity || !portAvailable(source)) {
            fx->inputChannel = -1;
            if (capacity) result.error = "Track input is unavailable: " + source;
            continue;
        }

        if (fx->source != source || fx->inputChannel < 0 || channelCapacity == 1) {
            fx->inputChannel = connector ? connector(source) : -1;
            fx->source = fx->inputChannel < 0 ? std::string() : source;
        }

        if (fx->inputChannel < 0) {
            if (lastErrorFn) result.error = lastErrorFn();
            if (result.error.empty()) result.error = "Could not connect track input: " + source;
            continue;
        }

        if (track->monitorEnabled && result.monitoredTrack < 0) {
            result.monitoredTrack = track->id;
        }
    }

    result.cleanMonitorEnabled = (result.monitoredTrack >= 0);
    return result;
}

SnapshotBuilder::RoutingResult SnapshotBuilder::syncRouting(
    const Project& project,
    std::unordered_map<int, TrackEffects>& effects,
    const std::string& defaultInput,
    const std::vector<std::string>& availableInputPorts,
    int channelCapacity,
    const ChannelConnector& connector,
    const ErrorCallback& lastErrorFn)
{
    return syncRouting(
        project,
        [&effects](int id) -> TrackEffects* {
            auto it = effects.find(id);
            return it != effects.end() ? &it->second : nullptr;
        },
        defaultInput,
        availableInputPorts,
        channelCapacity,
        connector,
        lastErrorFn);
}

std::unique_ptr<EngineSnapshot> SnapshotBuilder::buildPlaybackSnapshot(
    const Context& context,
    const AudioResolver& audioResolver,
    const EffectResolver& effectResolver,
    bool multiMonitor)
{
    auto snapshot = std::make_unique<EngineSnapshot>();
    snapshot->tempo = context.tempo;
    snapshot->multiMonitor = multiMonitor;
    snapshot->masterGain = context.masterVolume;
    snapshot->masterMuted = context.masterMuted;
    snapshot->playbackEnd = -1;

    for (const auto& track : context.tracks) {
        TrackRender render;
        render.trackId = track.id;
        render.gain = track.volume;
        const auto pan = panlaw::balance(track.pan);
        render.panLeft = pan.left;
        render.panRight = pan.right;
        render.muted = track.muted;
        render.soloed = track.solo;

        for (const auto& clip : track.audioClips) {
            const double rate = context.tempo.sampleRate() > 0.0 ? context.tempo.sampleRate() : 48000.0;
            // Playback stops one second after a clip whose sound can ring on: pattern notes, or
            // recorded audio through an instrument track's effects (added below once the audio
            // resolves). A clip with neither ends exactly at its end.
            const std::int64_t releaseTail = static_cast<std::int64_t>(std::llround(rate * 1.0));
            snapshot->playbackEnd = std::max(snapshot->playbackEnd,
                clip.start + clip.length + (clip.notes.empty() ? 0 : releaseTail));

            const auto beatsToSamples = [&](double beats) -> std::int64_t {
                if (context.tempo.bpm() <= 0.0 || context.tempo.sampleRate() <= 0.0) return 0;
                return context.tempo.sampleAt(beats);
            };
            const std::int64_t origin = clip.start - clip.sourceOffset;
            const std::int64_t clipEnd = clip.start + clip.length;

            for (const auto& n : clip.notes) {
                std::int64_t on = origin + beatsToSamples(n.startBeat);
                std::int64_t off = origin + beatsToSamples(n.startBeat + n.lengthBeats);
                on = std::max(on, clip.start);
                off = std::min(off, clipEnd);
                if (off <= on) continue;

                render.notes.push_back({
                    on,
                    off,
                    static_cast<uint8_t>(std::clamp(n.note, 0, 127)),
                    static_cast<uint8_t>(std::clamp(n.velocity, 0, 127))
                });
            }

            if (!audioResolver) continue;
            auto audio = audioResolver(clip.poolId);
            if (!audio) continue;
            if (track.kind == TrackKind::Instrument)
                snapshot->playbackEnd = std::max(snapshot->playbackEnd, clip.start + clip.length + releaseTail);
            render.clips.push_back({
                audio.get(),
                clip.start,
                clip.length,
                clip.sourceOffset,
                clip.fadeIn,
                clip.fadeOut,
                static_cast<float>(std::pow(10.0, clip.gainDb / 20.0)),
                clip.effectsPrinted
            });
            snapshot->keepAudio.push_back(std::move(audio));
        }

        std::stable_sort(render.clips.begin(), render.clips.end(), [](const ClipRender& a, const ClipRender& b) {
            return a.start < b.start;
        });

        std::stable_sort(render.notes.begin(), render.notes.end(), [](const NoteRender& a, const NoteRender& b) {
            return a.on < b.on;
        });

        render.noteOffs = render.notes;
        std::stable_sort(render.noteOffs.begin(), render.noteOffs.end(), [](const NoteRender& a, const NoteRender& b) {
            return a.off < b.off;
        });

        if (effectResolver) {
            const auto* fx = effectResolver(track.id);
            if (fx) {
                render.meters = fx->meters;
                render.inputChannel = fx->inputChannel;
                render.armed = (track.hasLiveInput || track.kind == TrackKind::Instrument) && track.recordArmed;
                render.monitoring = (track.hasLiveInput && track.monitorEnabled && fx->inputChannel >= 0)
                                 || (track.kind == TrackKind::Instrument && track.monitorEnabled);

                if (fx->instrument) {
                    render.chain.push_back(fx->instrument.get());
                    snapshot->keepProcessors.push_back(fx->instrument);
                }

                // Input trim first, then the track's effects in list order (plan 6.1). An effect
                // that is not on the list does not run.
                const auto addEffects = [&](std::vector<Processor*>& out, bool playback) {
                    for (const auto& type : track.effects) {
                        std::shared_ptr<Processor> processor;
                        if (type == effectchain::Eq) processor = playback ? fx->playbackEq : fx->eq;
                        else if (type == effectchain::ChaosEq) processor = playback ? fx->playbackChaosEq : fx->chaosEq;
                        else if (type == effectchain::GuitarSuite) processor = playback ? fx->playbackChain : fx->chain;
                        if (!processor) continue;
                        out.push_back(processor.get());
                        snapshot->keepProcessors.push_back(std::move(processor));
                    }
                };
                if (fx->input) {
                    render.monitorChain.push_back(fx->input.get());
                    snapshot->keepProcessors.push_back(fx->input);
                }
                addEffects(render.monitorChain, false);

                bool hasUnprintedClips = false;
                for (const auto& c : render.clips) {
                    if (!c.effectsPrinted) {
                        hasUnprintedClips = true;
                        break;
                    }
                }

                if (hasUnprintedClips || !render.notes.empty()) {
                    if (fx->playbackInput) {
                        render.chain.push_back(fx->playbackInput.get());
                        snapshot->keepProcessors.push_back(fx->playbackInput);
                    }
                    addEffects(render.chain, true);
                }
            }
        }

        snapshot->tracks.push_back(std::move(render));
    }

    return snapshot;
}

std::unique_ptr<EngineSnapshot> SnapshotBuilder::buildPlaybackSnapshot(
    const Context& context,
    const AudioResolver& audioResolver,
    const std::unordered_map<int, TrackEffects>& effects,
    bool multiMonitor)
{
    return buildPlaybackSnapshot(
        context,
        audioResolver,
        [&effects](int id) -> const TrackEffects* {
            auto it = effects.find(id);
            return it != effects.end() ? &it->second : nullptr;
        },
        multiMonitor);
}

std::unique_ptr<EngineSnapshot> SnapshotBuilder::buildPlaybackSnapshot(
    const Project& project,
    const AudioResolver& audioResolver,
    const EffectResolver& effectResolver,
    bool multiMonitor)
{
    return buildPlaybackSnapshot(Context::fromProject(project), audioResolver, effectResolver, multiMonitor);
}

std::unique_ptr<EngineSnapshot> SnapshotBuilder::buildPlaybackSnapshot(
    const Project& project,
    const AudioResolver& audioResolver,
    const std::unordered_map<int, TrackEffects>& effects,
    bool multiMonitor)
{
    return buildPlaybackSnapshot(Context::fromProject(project), audioResolver, effects, multiMonitor);
}

std::future<std::unique_ptr<EngineSnapshot>> SnapshotBuilder::buildPlaybackSnapshotAsync(
    Context context,
    AudioResolver audioResolver,
    std::unordered_map<int, TrackEffects> effects,
    bool multiMonitor)
{
    return std::async(
        std::launch::async,
        [ctx = std::move(context), resolver = std::move(audioResolver), fx = std::move(effects), multiMonitor]() mutable {
            return buildPlaybackSnapshot(ctx, resolver, fx, multiMonitor);
        });
}

std::unique_ptr<EngineSnapshot> SnapshotBuilder::buildMasterChainSnapshot(std::shared_ptr<Processor> chain)
{
    auto snapshot = std::make_unique<EngineSnapshot>();
    if (chain) {
        snapshot->masterChain.push_back(chain.get());
        snapshot->keepProcessors.push_back(std::move(chain));
    }
    return snapshot;
}

std::unique_ptr<EngineSnapshot> SnapshotBuilder::buildEmptySnapshot()
{
    return std::make_unique<EngineSnapshot>();
}
