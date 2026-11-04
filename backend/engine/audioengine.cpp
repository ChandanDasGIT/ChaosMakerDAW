#include "audioengine.h"

#include <algorithm>
#include <cmath>
#include <utility>

AudioEngine::AudioEngine(std::unique_ptr<AudioDevice> device)
    : m_device(std::move(device))
{
}

AudioEngine::~AudioEngine()
{
    stop();
    collectGarbage();
    delete m_snapshot.releaseCurrentAfterStop();
    delete m_snapshot.cancelPendingAfterStop();
    delete m_playback.releaseCurrentAfterStop();
    delete m_playback.cancelPendingAfterStop();
}

bool AudioEngine::setTempoBpm(double bpm) noexcept
{
    if (!std::isfinite(bpm) || bpm < 0.01 || bpm > 10000.0) return false;
    m_tempoBpm.store(bpm, std::memory_order_relaxed);
    return true;
}

bool AudioEngine::setInputTrimDb(double db) noexcept
{
    if (!std::isfinite(db) || db < -24 || db > 24) return false;
    m_inputTrimGain.store(float(std::pow(10.0, db / 20.0)), std::memory_order_relaxed);
    return true;
}

AudioEngine::MonitorLevels AudioEngine::takeMonitorLevels() noexcept
{
    return {m_rawMeter.takePeak(), m_preFxMeter.takePeak(), m_leftMeter.takePeak(), m_rightMeter.takePeak(),
            m_rawMeter.clipped(), m_preFxMeter.clipped(), m_leftMeter.clipped(), m_rightMeter.clipped()};
}

void AudioEngine::clearMonitorClips() noexcept
{
    m_rawMeter.clearClip(); m_preFxMeter.clearClip(); m_leftMeter.clearClip(); m_rightMeter.clearClip();
}

bool AudioEngine::start(int wantedBufferFrames)
{
    return m_device && m_device->open(this, wantedBufferFrames);
}

void AudioEngine::stop()
{
    if (m_device)
        m_device->close();
    finishRecording();
    EngineCommand pending;
    while (m_commands.pop(pending)) {
        if(pending.type==EngineCommand::Type::StartRecordingGroup && pending.group) {
            for(auto& entry:pending.group->entries)entry.recorder->releaseFromAudio();
            pending.group->released.store(true,std::memory_order_release);
        }
        if (pending.type == EngineCommand::Type::StartRecording && pending.recorder) {
            pending.recorder->finish();
            m_releasedRecorder.store(pending.recorder, std::memory_order_release);
        }
    }
    m_playing = false;
    m_reportedPlaying.store(false);
    m_reportedRecording.store(false);
}

void AudioEngine::publish(std::unique_ptr<EngineSnapshot> snapshot)
{
    tryPublish(snapshot);
}

bool AudioEngine::tryPublish(std::unique_ptr<EngineSnapshot>& snapshot)
{
    if (!snapshot || !m_snapshot.pendingEmpty())
        return false;
    m_snapshot.publish(snapshot.release());
    return true;
}

void AudioEngine::publishPlayback(std::unique_ptr<EngineSnapshot> snapshot)
{
    collectGarbage();
    delete m_playback.replacePending(snapshot.release());
}

void AudioEngine::publishMonitor(std::unique_ptr<EngineSnapshot> snapshot)
{
    collectGarbage();
    delete m_snapshot.replacePending(snapshot.release());
}

bool AudioEngine::tryPublishPlayback(std::unique_ptr<EngineSnapshot>& snapshot)
{
    if (!snapshot || !m_playback.pendingEmpty()) return false;
    m_playback.publish(snapshot.release()); return true;
}

void AudioEngine::collectGarbage()
{
    while (EngineSnapshot* retired = m_snapshot.collect())
        delete retired;
    while (EngineSnapshot* retired = m_playback.collect()) delete retired;
}

DeviceLatency AudioEngine::latency() const
{
    return m_device ? m_device->latency() : DeviceLatency{};
}

double AudioEngine::sampleRate() const
{
    return m_device ? m_device->sampleRate() : 0.0;
}

bool AudioEngine::setBufferSize(int frames)
{
    return m_device && m_device->setBufferSize(frames);
}

std::string AudioEngine::lastError() const
{
    return m_device ? m_device->lastError() : "No audio device configured.";
}

std::vector<std::string> AudioEngine::inputNames() const
{
    return m_device ? m_device->inputNames() : std::vector<std::string>{};
}

std::vector<std::string> AudioEngine::outputNames() const
{
    return m_device ? m_device->outputNames() : std::vector<std::string>{};
}

bool AudioEngine::connectInput(const std::string& source)
{
    return m_device && m_device->connectInput(source);
}

int AudioEngine::connectInputChannel(const std::string& source)
{
    return m_device ? m_device->connectInputChannel(source) : -1;
}

bool AudioEngine::connectOutput(const std::string& left, const std::string& right)
{
    return m_device && m_device->connectOutput(left, right);
}

void AudioEngine::renderBlock(const AudioIO& io) noexcept
{
    process(io);
}

void AudioEngine::process(const AudioIO& io) noexcept
{
    DenormalGuard denormalGuard;
    EngineCommand command;
    while (m_commands.pop(command)) {
        if (command.type == EngineCommand::Type::SetCleanMonitor)
            m_cleanMonitorEnabled = command.a != 0;
        else if (command.type == EngineCommand::Type::SetCleanMonitorGain)
            m_cleanMonitorGain = command.value;
        else switch (command.type) {
        case EngineCommand::Type::SetMetronome:
            m_metronomeEnabled=command.a!=0;
            if (std::isfinite(command.value)) m_metronomeVolume=std::clamp(command.value,0.0f,1.0f);
            break;
        case EngineCommand::Type::Play: finishRecording(); m_resetPlayback = true; m_playing = true; break;
        case EngineCommand::Type::Pause: finishRecording(); m_resetPlayback = true; m_playing = false; break;
        case EngineCommand::Type::Stop: finishRecording(); m_resetPlayback = true; m_playing = false; m_position = 0; break;
        case EngineCommand::Type::Seek:
            if (!m_recorder && !m_recordingGroup) { m_position = std::clamp<std::int64_t>(command.a, 0, INT64_MAX / 2); m_resetPlayback = true; }
            break;
        case EngineCommand::Type::SetLoop:
            m_loopStart = std::max<std::int64_t>(0, command.a); m_loopEnd = command.b;
            m_looping = m_loopEnd > m_loopStart; break;
        case EngineCommand::Type::ClearLoop: m_looping = false; break;
        case EngineCommand::Type::StartRecording:
            if (command.recorder && !m_recorder) {
                m_recorder = command.recorder; m_recorder->begin(m_position);
                m_recordingStart.store(m_position, std::memory_order_relaxed); m_playing = true;
            }
            break;
        case EngineCommand::Type::StartRecordingGroup:
            if(command.group && !m_recordingGroup && !m_recorder) {
                m_recordingGroup=command.group;
                for(auto& entry:m_recordingGroup->entries)entry.recorder->begin(m_position);
                m_recordingStart.store(m_position,std::memory_order_relaxed);
                if (command.a>0 && command.a<INT64_MAX/4) {
                    m_countInTarget=m_position; m_position-=command.a;
                }
                m_playing=true;
            }
            break;
        case EngineCommand::Type::StopRecording:
            if (m_countInTarget>=0) m_playing=false;
            finishRecording(); break;
        default: break;
        }
    }

    EngineSnapshot* snapshot = m_snapshot.acquire();
    EngineSnapshot* playback = m_playback.acquire();
    const double bpm = m_tempoBpm.load(std::memory_order_relaxed);

    io.output.clear();
    if(playback && playback->multiMonitor) {
        renderTracks(io,*playback,std::max(0,std::min(io.frames,io.output.frames)));
        m_reportedPosition.store(std::max<std::int64_t>(0,m_position),std::memory_order_relaxed);
        m_countInRemaining.store(m_countInTarget>=0 ? std::max<std::int64_t>(0,m_countInTarget-m_position) : 0,std::memory_order_relaxed);
        m_reportedPlaying.store(m_playing,std::memory_order_relaxed);
        m_reportedRecording.store(m_recordingGroup!=nullptr && m_countInTarget<0,std::memory_order_relaxed);
        return;
    }
    if (io.input.channelCount > 0 && io.input.frames > 0 && io.frames > 0) {
        AudioView raw = io.input;
        raw.channelCount = 1; // The live monitor currently consumes mono input 0.
        raw.frames = std::min(io.frames, io.input.frames);
        m_rawMeter.capture(raw);
    }
    const int renderedFrames = std::max(0, std::min(io.frames, io.output.frames));
    if (m_recorder && renderedFrames > 0) {
        if (io.input.channelCount <= 0 || io.input.frames < renderedFrames
            || (m_recorder->source() == TakeRecorder::Source::DryInput
                && !m_recorder->capture(io.input.channel(0), renderedFrames))) {
            finishRecording(); m_playing = false;
        }
    }
    const bool recordFx = m_recorder && m_recorder->source() == TakeRecorder::Source::PostFx;
    TrackMeters* liveMeters = snapshot ? snapshot->monitorMeters.get() : nullptr;
    const bool liveActive = (m_cleanMonitorEnabled || recordFx) && io.input.channelCount > 0
        && io.input.frames > 0 && renderedFrames > 0;
    if (liveActive) {

        // Analysis receives the unmodified input, independent of pedal order and EQ.
        ProcessContext analysis;
        analysis.audio = io.input;
        analysis.audio.frames = std::min(io.frames, io.input.frames);
        analysis.sampleRate = m_sampleRate;
        analysis.bpm = bpm;
        if (snapshot)
            for (Processor* processor : snapshot->masterChain)
                if (processor) processor->analyseInput(analysis);

        const float* input = io.input.channel(0);
        const int frames = std::min(renderedFrames, io.input.frames);
        float* channels[]{m_fxAudio[0].data(), m_fxAudio[1].data()};
        const float targetTrim = m_inputTrimGain.load(std::memory_order_relaxed);
        for (int offset = 0; offset < frames; offset += m_fxBlockFrames) {
            const int count = std::min(m_fxBlockFrames, frames - offset);
            for (int i = 0; i < count; ++i) {
                const float sample = input[offset + i]
                    * (snapshot && snapshot->inputStage ? 1.0f : m_inputTrim.next(targetTrim));
                channels[0][i] = channels[1][i] = sample;
            }
            ProcessContext context;
            context.audio = AudioView{channels, 2, count};
            context.sampleRate = m_sampleRate;
            context.bpm = bpm;
            if (snapshot && snapshot->inputStage) snapshot->inputStage->process(context);
            else m_monitorEq.process(context);
            m_preFxMeter.capture(context.audio);
            if (snapshot) {
                for (Processor* processor : snapshot->masterChain)
                    if (processor) processor->process(context);
            }
            // Print only the instrument FX, before monitor gain and playback mixing.
            if (recordFx && m_recorder && !m_recorder->capture(context.audio)) {
                finishRecording(); m_playing = false;
            }
            const float monitorGain = m_cleanMonitorEnabled ? m_cleanMonitorGain : 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < count; ++i) channels[c][i] *= monitorGain;
            if (liveMeters && !m_playing) {
                float* raw[]{io.input.channel(0) + offset};
                for (int c = 0; c < 2; ++c) {
                    liveMeters->input[c].capture({raw,1,count});
                    liveMeters->output[c].capture({channels+c,1,count});
                }
            }
            m_leftMeter.capture(AudioView{channels, 1, count});
            m_rightMeter.capture(AudioView{channels + 1, 1, count});
            for (int c = 0; c < io.output.channelCount; ++c)
                std::copy_n(channels[c == 1 ? 1 : 0], count, io.output.channel(c) + offset);
        }
    }
    mixPlayback(io, playback, renderedFrames, liveActive ? liveMeters : nullptr);
    const float masterVol = m_masterMuted.load(std::memory_order_relaxed) ? 0.0f
        : (playback && playback->masterMuted ? 0.0f : m_masterVolume.load(std::memory_order_relaxed));
    for (int c = 0; c < io.output.channelCount; ++c) {
        float* ch = io.output.channel(c);
        for (int i = 0; i < renderedFrames; ++i) {
            ch[i] = std::clamp(ch[i] * masterVol, -1.0f, 1.0f);
        }
    }
    if (io.output.channelCount > 0 && renderedFrames > 0) {
        float* leftCh = io.output.channel(0);
        float* rightCh = io.output.channel(std::min(1, io.output.channelCount - 1));
        m_leftMeter.capture(AudioView{&leftCh, 1, renderedFrames});
        m_rightMeter.capture(AudioView{&rightCh, 1, renderedFrames});
    }
    m_reportedPosition.store(m_position, std::memory_order_relaxed);
    m_reportedPlaying.store(m_playing, std::memory_order_relaxed);
    m_reportedRecording.store(m_recorder != nullptr, std::memory_order_relaxed);
}

void AudioEngine::deviceChanged(double sampleRate, int maxFrames)
{
    m_sampleRate = sampleRate;
    m_fxBlockFrames = std::clamp(maxFrames, 1, int(m_fxAudio[0].size()));
    m_monitorEq.prepare(sampleRate, maxFrames);
    m_inputTrim.prepare(sampleRate, 20.0f);
    m_inputTrim.reset(m_inputTrimGain.load(std::memory_order_relaxed));
}

void AudioEngine::xrunOccurred() noexcept
{
    m_events.push(EngineEvent{EngineEvent::Type::Xrun, 0});
}

void AudioEngine::finishRecording() noexcept
{
    if (m_countInTarget>=0) {
        if (m_position<m_countInTarget) m_position=m_countInTarget;
        m_countInTarget=-1;
    }
    m_countInRemaining.store(0,std::memory_order_relaxed);
    if(m_recordingGroup) {
        auto* group=m_recordingGroup; m_recordingGroup=nullptr;
        for(auto& entry:group->entries) {
            if(entry.active) { entry.recorder->releaseFromAudio(); entry.active=false; }
        }
        group->released.store(true,std::memory_order_release);
    }
    if (!m_recorder) return;
    TakeRecorder* finished = m_recorder;
    m_recorder = nullptr;
    finished->finish();
    m_releasedRecorder.store(finished, std::memory_order_release);
}

void AudioEngine::mixMetronome(const AudioIO& io, int offset, int frames, const TempoMap& tempo) noexcept
{
    if (!m_playing || (!m_metronomeEnabled && m_countInTarget<0) || m_metronomeVolume<=0) return;
    const double period=m_sampleRate*60/m_tempoBpm.load(std::memory_order_relaxed)*4/tempo.denominator();
    if (!std::isfinite(period) || period<=0 || tempo.numerator()<=0) return;
    for (int i=0;i<frames;++i) {
        const double position=double(m_position+i);
        const double beat=std::floor((position+0.5)/period);
        const double age=position-std::round(beat*period);
        if (age<0 || age>=m_sampleRate*0.02) continue;
        const bool accent=std::fmod(beat,double(tempo.numerator()))==0;
        const double time=age/m_sampleRate;
        const float click=float((accent ? 0.35 : 0.22)*m_metronomeVolume
            *std::exp(-time/0.004)*std::sin(6.283185307179586*(accent ? 1760 : 1100)*time));
        for (int c=0;c<io.output.channelCount;++c) io.output.channel(c)[offset+i]+=click;
    }
}

void AudioEngine::mixPlayback(const AudioIO& io, const EngineSnapshot* snapshot, int frames, TrackMeters* liveMeters) noexcept
{
    bool anySolo = false;
    if (snapshot) for (const auto& track : snapshot->tracks) {
        anySolo |= track.soloed;
        if (m_resetPlayback) for (auto* processor : track.chain) if (processor) processor->reset();
    }
    m_resetPlayback = false;
    const bool bounded=snapshot && snapshot->playbackEnd>=0 && !m_looping && !m_recorder;
    for (int offset = 0; offset < frames && m_playing;) {
        if (bounded && m_position>=snapshot->playbackEnd) {
            m_position=snapshot->playbackEnd; m_playing=false; m_resetPlayback=true; break;
        }
        if (m_looping && !m_recorder && m_position >= m_loopEnd) m_position = m_loopStart;
        int count = std::min(m_fxBlockFrames, frames-offset);
        if (bounded) count=int(std::min<std::int64_t>(count,snapshot->playbackEnd-m_position));
        if (m_looping && !m_recorder) count = int(std::min<std::int64_t>(count,m_loopEnd-m_position));
        for (int c=0;c<2;++c) {
            if (io.output.channelCount>0) std::copy_n(io.output.channel(std::min(c,io.output.channelCount-1))+offset,count,m_liveAudio[c].data());
            else std::fill_n(m_liveAudio[c].data(),count,0);
        }
        if (snapshot) for (const auto& track : snapshot->tracks) {
            const bool live = liveMeters && track.meters.get()==liveMeters;
            const bool audible = !track.muted && (!anySolo || track.soloed);
            float* channels[]{m_playbackAudio[0].data(),m_playbackAudio[1].data()};
            std::fill_n(channels[0],count,0); std::fill_n(channels[1],count,0);
            for (auto& channel : m_printedAudio) std::fill_n(channel.data(),count,0);
            for (const auto& clip : track.clips) {
                if (!clip.audio || clip.audio->channels.empty()) continue;
                const bool dry = !clip.effectsPrinted && !track.chain.empty();
                for (int i=0;i<count;++i) {
                    const auto position=m_position+i;
                    if(position<clip.start || position-clip.start>=clip.length)continue;
                    const double source=clip.sourceOffset+double(position-clip.start)*clip.audio->sampleRate/m_sampleRate;
                    if(source<0 || source>=clip.audio->frames)continue;
                    const auto at=static_cast<std::size_t>(source);
                    const int channelCount=2;
                    for(int c=0;c<channelCount;++c) {
                        const auto& data=clip.audio->channels[std::min<std::size_t>(c,clip.audio->channels.size()-1)];
                        if(at>=data.size())continue;
                        const float next=at+1<data.size()?data[at+1]:data[at];
                        const float sample=(data[at]+float(source-at)*(next-data[at]))*clip.gain;
                        if(dry)channels[c][i]+=sample;
                        else m_printedAudio[c][i]+=sample;
                    }
                }
            }
            if (track.meters) for(int c=0;c<2;++c) {
                float peak=0;
                // The stereo track input includes all source clips and any live input.
                for(int i=0;i<count;++i) {
                    float sample=channels[c][i]+m_printedAudio[c][i];
                    if(live && offset+i<io.input.frames) sample+=io.input.channel(0)[offset+i];
                    if(std::isfinite(sample)) peak=std::max(peak,std::abs(sample));
                }
                track.meters->input[c].capturePeak(peak);
            }
            if (!track.chain.empty()) {
                ProcessContext context; context.audio={channels,2,count}; context.sampleRate=m_sampleRate;
                context.samplePosition=m_position; context.bpm=m_tempoBpm.load(std::memory_order_relaxed); context.playing=true;
                for(auto* processor:track.chain)if(processor)processor->process(context);
            }
            for(int c=0;c<2;++c) {
              float peak=0;
              for(int i=0;i<count;++i) {
                const float sample=audible ? (channels[c][i]+m_printedAudio[c][i])*track.gain : 0.0f;
                if(track.meters) {
                    float measured=sample+(live ? m_liveAudio[c][i] : 0.0f);
                    if(std::isfinite(measured)) peak=std::max(peak,std::abs(measured));
                }
                if(c<io.output.channelCount) io.output.channel(c)[offset+i]+=sample;
              }
              if(track.meters)track.meters->output[c].capturePeak(peak);
            }
            for(int c=2;c<io.output.channelCount;++c) for(int i=0;i<count;++i)
                if(audible)io.output.channel(c)[offset+i]+=(channels[0][i]+m_printedAudio[0][i])*track.gain;
        }
        mixMetronome(io,offset,count,snapshot ? snapshot->tempo : TempoMap{});
        m_position=std::min<std::int64_t>(INT64_MAX/2,m_position+count); offset+=count;
        if (bounded && m_position==snapshot->playbackEnd) { m_playing=false; m_resetPlayback=true; }
    }
    for(int c=0;c<io.output.channelCount;++c)for(int i=0;i<frames;++i) {
        float& sample=io.output.channel(c)[i]; sample=std::isfinite(sample)?std::clamp(sample,-1.0f,1.0f):0.0f;
    }
}

void AudioEngine::renderTracks(const AudioIO& io, const EngineSnapshot& snapshot, int frames) noexcept
{
    bool anySolo=false;
    for(const auto& track:snapshot.tracks) {
        anySolo|=track.soloed;
        if(m_resetPlayback)for(auto* processor:track.chain)if(processor)processor->reset();
    }
    m_resetPlayback=false;
    if(m_recordingGroup) {
        bool active=false, missingInput=false;
        for(auto& entry:m_recordingGroup->entries) {
            if(!entry.active)continue;
            const auto track=std::find_if(snapshot.tracks.begin(),snapshot.tracks.end(),[&](const auto& t){return t.trackId==entry.trackId;});
            if(track==snapshot.tracks.end() || !track->armed) {
                entry.recorder->releaseFromAudio(); entry.active=false;
            } else {
                active=true;
                missingInput|=entry.inputChannel<0 || entry.inputChannel>=io.input.channelCount || io.input.frames<frames;
            }
        }
        if(!active || missingInput) {
            const bool wasCountingIn=m_countInTarget>=0;
            if(missingInput)m_recordingGroup->inputLost.store(true,std::memory_order_relaxed);
            finishRecording(); if(missingInput || wasCountingIn)m_playing=false;
        }
    }
    for(int offset=0;offset<frames;) {
        if (m_countInTarget>=0 && m_position>=m_countInTarget) m_countInTarget=-1;
        const bool bounded=snapshot.playbackEnd>=0 && !m_looping && !m_recordingGroup;
        if (bounded && m_playing && m_position>=snapshot.playbackEnd) {
            m_position=snapshot.playbackEnd; m_playing=false; m_resetPlayback=true;
        }
        if(m_playing && m_looping && !m_recordingGroup && m_position>=m_loopEnd)m_position=m_loopStart;
        int count=std::min(m_fxBlockFrames,frames-offset);
        if (m_countInTarget>=0) count=int(std::min<std::int64_t>(count,m_countInTarget-m_position));
        if (bounded && m_playing) count=int(std::min<std::int64_t>(count,snapshot.playbackEnd-m_position));
        if(m_playing && m_looping && !m_recordingGroup)count=int(std::min<std::int64_t>(count,m_loopEnd-m_position));
        bool failed=false;
        for(const auto& track:snapshot.tracks) {
            RecordingGroup::Entry* recording=nullptr;
            if(m_recordingGroup)for(auto& entry:m_recordingGroup->entries)
                if(entry.active && entry.trackId==track.trackId) { recording=&entry; break; }
            const int inputChannel=recording ? recording->inputChannel : track.inputChannel;
            const bool live=(track.monitoring || recording) && inputChannel>=0
                && inputChannel<io.input.channelCount && io.input.frames>=offset+count;
            const bool audible=!track.muted && (!anySolo || track.soloed);
            float* liveAudio[]{m_fxAudio[0].data(),m_fxAudio[1].data()};
            float* dry[]{m_playbackAudio[0].data(),m_playbackAudio[1].data()};
            for(int c=0;c<2;++c) {
                std::fill_n(liveAudio[c],count,0); std::fill_n(dry[c],count,0);
                std::fill_n(m_printedAudio[c].data(),count,0);
            }
            if(live) {
                float* raw[]{io.input.channel(inputChannel)+offset};
                for(int c=0;c<2;++c)std::copy_n(raw[0],count,liveAudio[c]);
                ProcessContext context; context.audio={liveAudio,2,count}; context.sampleRate=m_sampleRate;
                context.samplePosition=m_position; context.bpm=m_tempoBpm.load(std::memory_order_relaxed); context.playing=m_playing;
                ProcessContext analysis=context; analysis.audio={raw,1,count};
                for(auto* processor:track.monitorChain)if(processor)processor->analyseInput(analysis);
                if(track.meters)track.meters->raw.capture({raw,1,count});
                for(std::size_t i=0;i<track.monitorChain.size();++i) {
                    if(track.monitorChain[i])track.monitorChain[i]->process(context);
                    if(i==0 && track.meters)track.meters->preFx.capture(context.audio);
                }
                if(recording && m_countInTarget<0) {
                    const bool captured=recording->recorder->source()==TakeRecorder::Source::DryInput
                        ? recording->recorder->capture(raw[0],count) : recording->recorder->capture(context.audio);
                    failed|=!captured;
                }
            }
            if(m_playing)for(const auto& clip:track.clips) {
                if(!clip.audio || clip.audio->channels.empty())continue;
                for(int i=0;i<count;++i) {
                    const auto at=m_position+i;
                    if(at<clip.start || at-clip.start>=clip.length)continue;
                    const double source=clip.sourceOffset+double(at-clip.start)*clip.audio->sampleRate/m_sampleRate;
                    if(source<0 || source>=clip.audio->frames)continue;
                    const auto sample=static_cast<std::size_t>(source);
                    for(int c=0;c<2;++c) {
                        const auto& data=clip.audio->channels[std::min<std::size_t>(c,clip.audio->channels.size()-1)];
                        if(sample>=data.size())continue;
                        const float next=sample+1<data.size()?data[sample+1]:data[sample];
                        const float value=(data[sample]+float(source-sample)*(next-data[sample]))*clip.gain;
                        if(!clip.effectsPrinted && !track.chain.empty())dry[c][i]+=value;
                        else m_printedAudio[c][i]+=value;
                    }
                }
            }
            if(track.meters)for(int c=0;c<2;++c) {
                float peak=0;
                for(int i=0;i<count;++i) {
                    const float value=dry[c][i]+m_printedAudio[c][i]+(live ? io.input.channel(inputChannel)[offset+i] : 0);
                    if(std::isfinite(value))peak=std::max(peak,std::abs(value));
                }
                track.meters->input[c].capturePeak(peak);
            }
            if(m_playing && !track.chain.empty()) {
                ProcessContext context; context.audio={dry,2,count}; context.sampleRate=m_sampleRate;
                context.samplePosition=m_position; context.bpm=m_tempoBpm.load(std::memory_order_relaxed); context.playing=true;
                for(auto* processor:track.chain)if(processor)processor->process(context);
            }
            for(int c=0;c<2;++c) {
                float peak=0;
                for(int i=0;i<count;++i) {
                    float value=0;
                    if(audible)value=(dry[c][i]+m_printedAudio[c][i]+(live && track.monitoring && m_cleanMonitorEnabled ? liveAudio[c][i] : 0))*track.gain;
                    liveAudio[c][i]=value;
                    if(std::isfinite(value))peak=std::max(peak,std::abs(value));
                }
                if(track.meters)track.meters->output[c].capturePeak(peak);
            }
            for(int c=0;c<io.output.channelCount;++c)for(int i=0;i<count;++i)
                io.output.channel(c)[offset+i]+=liveAudio[c==1?1:0][i];
        }
        mixMetronome(io,offset,count,snapshot.tempo);
        if(m_playing)m_position=std::min<std::int64_t>(INT64_MAX/2,m_position+count);
        if (m_countInTarget>=0 && m_position>=m_countInTarget) m_countInTarget=-1;
        if (bounded && m_playing && m_position==snapshot.playbackEnd) { m_playing=false; m_resetPlayback=true; }
        offset+=count;
        if(failed) {
            if(m_recordingGroup)m_recordingGroup->captureFailed.store(true,std::memory_order_relaxed);
            finishRecording(); m_playing=false; break;
        }
    }
    const float masterVol = m_masterMuted.load(std::memory_order_relaxed) ? 0.0f
        : (snapshot.masterMuted ? 0.0f : m_masterVolume.load(std::memory_order_relaxed));
    for(int c=0;c<io.output.channelCount;++c)for(int i=0;i<frames;++i) {
        float& value=io.output.channel(c)[i]; value=std::isfinite(value)?std::clamp(value * masterVol,-1.0f,1.0f):0;
    }
    if (io.output.channelCount > 0 && frames > 0) {
        float* leftCh = io.output.channel(0);
        float* rightCh = io.output.channel(std::min(1, io.output.channelCount - 1));
        m_leftMeter.capture(AudioView{&leftCh, 1, frames});
        m_rightMeter.capture(AudioView{&rightCh, 1, frames});
    }
}
