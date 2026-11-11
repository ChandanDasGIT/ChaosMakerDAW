#include "engine/snapshotbuilder.h"
#include "session/tracklist.h"
#include "instruments/chords/chordsinstrument.h"
#include "instruments/drums/drumkit.h"
#include "instruments/electrodrums/electrodrums.h"
#include "instruments/harmonium/harmonium.h"
#include "instruments/piano/piano.h"
#include "dsp/panlaw.h"
#include "plugins/mixfx/mixfxfactory.h"

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
        info.volumeEnvelope = track->volumeEnvelope;
        for (const auto& slot : track->effects) info.effects.push_back(slot.type);
        ctx.tracks.push_back(std::move(info));
    }
    for (const auto& slot : project.master().effects) ctx.masterEffects.push_back(slot.type);
    return ctx;
}

std::shared_ptr<Processor> SnapshotBuilder::processorFor(const TrackEffects& fx, const std::string& type, bool playback)
{
    if (type == effectchain::Eq) return playback ? fx.playbackEq : fx.eq;
    if (type == effectchain::ChaosEq) return playback ? fx.playbackChaosEq : fx.chaosEq;
    if (type == effectchain::GuitarSuite) return playback ? fx.playbackChain : fx.chain;
    const auto& set = playback ? fx.playbackMixEffects : fx.mixEffects;
    if (const auto it = set.find(type); it != set.end()) return it->second;
    return nullptr;
}

void SnapshotBuilder::ensureMixEffects(TrackEffects& effects, const std::vector<std::string>& types, double sampleRate, int maxFrames)
{
    for (const auto& type : types) {
        if (!mixfx::isMixEffect(type)) continue;
        for (auto* set : {&effects.mixEffects, &effects.playbackMixEffects}) {
            if (set->count(type)) continue;
            auto effect = mixfx::create(type);
            if (!effect) continue;
            effect->prepare(sampleRate, maxFrames);
            set->emplace(type, std::move(effect));
        }
    }
}

void SnapshotBuilder::remakeMixEffects(TrackEffects& effects, double sampleRate, int maxFrames)
{
    std::vector<std::string> types;
    for (const auto& [type, effect] : effects.mixEffects) types.push_back(type);
    effects.mixEffects.clear();
    effects.playbackMixEffects.clear();
    ensureMixEffects(effects, types, sampleRate, maxFrames);
}

void SnapshotBuilder::applyEffectState(TrackEffects& fx, const EffectState& state)
{
    for (int band = 0; band < 10; ++band) {
        if (fx.eq) fx.eq->setBandGain(band, state.eqGainDb[std::size_t(band)]);
        if (fx.playbackEq) fx.playbackEq->setBandGain(band, state.eqGainDb[std::size_t(band)]);
    }
    if (fx.chaosEq) fx.chaosEq->setSettings(state.chaosEq);
    if (fx.playbackChaosEq) fx.playbackChaosEq->setSettings(state.chaosEq);
    for (auto* set : {&fx.mixEffects, &fx.playbackMixEffects})
        for (auto& [type, effect] : *set)
            if (effect) effect->apply(state.mixEffect(type));
}

void SnapshotBuilder::syncTrackEq(const Project& project, const MutableEffectResolver& effectLookup)
{
    if (!effectLookup) return;
    // Clip effects (restMap R2): each clip that has some.
    for (const auto& track : project.tracks())
        for (const auto& clip : track->audioClips)
            if (clip.fx.hasEffects())
                if (auto* fx = effectLookup(Project::clipEffectsId(clip.id))) applyEffectState(*fx, clip.fx);
    std::vector<const Track*> all;
    for (const auto& track : project.tracks()) all.push_back(track.get());
    all.push_back(&project.master());   // the master track's EQs and plugins too (G9)
    for (const auto* track : all) {
        auto* fx = effectLookup(track->id);
        if (!fx) continue;
        applyEffectState(*fx, *track);
        if (fx->instrument && fx->instrument->typeId() == "harmonium")
            static_cast<Harmonium&>(*fx->instrument).setSettings(track->harmonium);
        if (fx->instrument && fx->instrument->typeId() == "drumkit")
            static_cast<DrumKit&>(*fx->instrument).setSettings(track->drumkit);
        if (fx->instrument && fx->instrument->typeId() == "electrodrums")
            static_cast<ElectroDrums&>(*fx->instrument).setSettings(track->electrodrums);
        if (fx->instrument && fx->instrument->typeId() == "piano")
            static_cast<Piano&>(*fx->instrument).setSettings(track->piano);
        if (fx->instrument && fx->instrument->typeId() == "chords") {
            auto& chordsTrack = static_cast<ChordsInstrument&>(*fx->instrument);
            chordsTrack.piano().setSettings(track->piano);
            chords::applyTo(chordsTrack.player(), track->chords);
        }
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
        render.envelope = track.volumeEnvelope;
        const auto pan = panlaw::balance(track.pan);
        render.panLeft = pan.left;
        render.panRight = pan.right;
        render.muted = track.muted;
        render.soloed = track.solo;

        for (const auto& clip : track.audioClips) {
            // Playback ends exactly where the last clip ends; what still rings fades out after
            // the transport stops (AudioEngine::endPlayback).
            snapshot->playbackEnd = std::max(snapshot->playbackEnd, clip.start + clip.length);

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
            // A reversed clip plays a reversed copy of its file (restMap G14).
            auto audio = audioResolver(clip.reversed ? TrackList::reversedPoolId(clip.poolId) : clip.poolId);
            if (!audio) continue;
            render.clips.push_back({
                audio.get(),
                clip.start,
                clip.length,
                clip.sourceOffset,
                clip.fadeIn,
                clip.fadeOut,
                static_cast<float>(std::pow(10.0, clip.gainDb / 20.0)) * (clip.polarityInverted ? -1.0f : 1.0f),
                clip.effectsPrinted,
                {}
            });
            snapshot->keepAudio.push_back(std::move(audio));
            // Clip effects (restMap R2): the playback instances, in the clip's order.
            if (clip.fx.hasEffects() && effectResolver) {
                if (const auto* clipFx = effectResolver(Project::clipEffectsId(clip.id))) {
                    for (const auto& slot : clip.fx.effects) {
                        auto processor = processorFor(*clipFx, slot.type, true);
                        if (!processor) continue;
                        render.clips.back().chain.push_back(processor.get());
                        snapshot->keepProcessors.push_back(std::move(processor));
                    }
                }
            }
        }

        std::stable_sort(render.clips.begin(), render.clips.end(), [](const ClipRender& a, const ClipRender& b) {
            return a.start < b.start;
        });
        {   // Fades as played (restMap G8): own fades plus the crossfade where audio clips overlap.
            std::vector<FadeSpan> spans;
            spans.reserve(render.clips.size());
            for (const auto& c : render.clips) spans.push_back({c.start, c.length, c.fadeIn, c.fadeOut});
            playedFades(spans);
            for (std::size_t i = 0; i < spans.size(); ++i) {
                render.clips[i].fadeIn = spans[i].fadeIn;
                render.clips[i].fadeOut = spans[i].fadeOut;
            }
        }

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
                        auto processor = processorFor(*fx, type, playback);
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

        // Delay compensation (restMap G13): a clip is read early by the delay of the effects it
        // goes through (its own, and the track's unless its effects are printed), so it is heard
        // in time. Live input and MIDI notes are not moved.
        std::int64_t trackDelay = 0;
        for (const auto* processor : render.chain) if (processor) trackDelay += std::max(0, processor->latencySamples());
        bool moved = false;
        for (auto& c : render.clips) {
            std::int64_t delay = 0;
            for (const auto* processor : c.chain) if (processor) delay += std::max(0, processor->latencySamples());
            c.tail = delay;
            if (!c.effectsPrinted) delay += trackDelay;
            c.start -= delay;
            moved |= delay > 0;
        }
        if (moved)
            std::stable_sort(render.clips.begin(), render.clips.end(), [](const ClipRender& a, const ClipRender& b) {
                return a.start < b.start;
            });

        snapshot->tracks.push_back(std::move(render));
    }

    // The master track's effects (G9), on the summed mix: the playback instances, in order.
    if (effectResolver && !context.masterEffects.empty()) {
        if (const auto* master = effectResolver(Project::MasterTrackId)) {
            for (const auto& type : context.masterEffects) {
                auto processor = processorFor(*master, type, true);
                if (!processor) continue;
                snapshot->masterEffects.push_back(processor.get());
                snapshot->keepProcessors.push_back(std::move(processor));
            }
        }
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
