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
        case EngineCommand::Type::Play: finishRecording(); m_playing = true; break;
        case EngineCommand::Type::Pause: finishRecording(); m_playing = false; break;
        case EngineCommand::Type::Stop: finishRecording(); m_playing = false; m_position = 0; break;
        case EngineCommand::Type::Seek:
            if (!m_recorder) m_position = std::clamp<std::int64_t>(command.a, 0, INT64_MAX / 2);
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
        case EngineCommand::Type::StopRecording: finishRecording(); break;
        default: break;
        }
    }

    EngineSnapshot* snapshot = m_snapshot.acquire();
    EngineSnapshot* playback = m_playback.acquire();
    const double bpm = m_tempoBpm.load(std::memory_order_relaxed);

    io.output.clear();
    if (io.input.channelCount > 0 && io.input.frames > 0 && io.frames > 0) {
        AudioView raw = io.input;
        raw.channelCount = 1; // The live monitor currently consumes mono input 0.
        raw.frames = std::min(io.frames, io.input.frames);
        m_rawMeter.capture(raw);
    }
    const int renderedFrames = std::max(0, std::min(io.frames, io.output.frames));
    if (m_recorder && renderedFrames > 0) {
        if (io.input.channelCount <= 0 || io.input.frames < renderedFrames
            || !m_recorder->capture(io.input.channel(0), renderedFrames)) {
            finishRecording(); m_playing = false;
        }
    }
    if (m_cleanMonitorEnabled && io.input.channelCount > 0 && io.output.channelCount > 0
        && io.input.frames > 0 && io.output.frames > 0) {

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
        const int frames = std::min(io.frames, std::min(io.input.frames, io.output.frames));
        const int channelsCount = std::min(io.output.channelCount, 2);
        float* channels[2]{};
        for (int channel = 0; channel < channelsCount; ++channel)
            channels[channel] = io.output.channel(channel);
        const float targetTrim = m_inputTrimGain.load(std::memory_order_relaxed);
        for (int frame = 0; frame < frames; ++frame) {
            const float sample = input[frame] * m_inputTrim.next(targetTrim);
            for (int channel = 0; channel < channelsCount; ++channel)
                channels[channel][frame] = sample;
        }
        ProcessContext context;
        context.audio = AudioView{channels, channelsCount, frames};
        context.sampleRate = m_sampleRate;
        context.bpm = bpm;
        m_monitorEq.process(context);
        m_preFxMeter.capture(context.audio);
        if (snapshot) {
            for (Processor* processor : snapshot->masterChain) {
                if (processor)
                    processor->process(context);
            }
        }
        for (int c = 0; c < channelsCount; ++c)
            for (int i = 0; i < frames; ++i) channels[c][i] *= m_cleanMonitorGain;
        m_leftMeter.capture(AudioView{channels, 1, frames});
        if (channelsCount > 1) m_rightMeter.capture(AudioView{channels + 1, 1, frames});
        for (int channel = channelsCount; channel < io.output.channelCount; ++channel)
            std::copy_n(channels[0], frames, io.output.channel(channel));
    }
    mixPlayback(io, playback, renderedFrames);
    m_reportedPosition.store(m_position, std::memory_order_relaxed);
    m_reportedPlaying.store(m_playing, std::memory_order_relaxed);
    m_reportedRecording.store(m_recorder != nullptr, std::memory_order_relaxed);
}

void AudioEngine::deviceChanged(double sampleRate, int maxFrames)
{
    m_sampleRate = sampleRate;
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
    if (!m_recorder) return;
    TakeRecorder* finished = m_recorder;
    m_recorder = nullptr;
    finished->finish();
    m_releasedRecorder.store(finished, std::memory_order_release);
}

void AudioEngine::mixPlayback(const AudioIO& io, const EngineSnapshot* snapshot, int frames) noexcept
{
    bool anySolo = false;
    if (snapshot) for (const auto& track : snapshot->tracks) anySolo |= track.soloed;
    for (int i = 0; i < frames; ++i) {
        if (m_playing) {
            // Recording is linear; repeat resumes only after the take has finished.
            if (m_looping && !m_recorder && m_position >= m_loopEnd) m_position = m_loopStart;
            if (snapshot) for (const auto& track : snapshot->tracks) {
                if (track.muted || (anySolo && !track.soloed)) continue;
                for (const auto& clip : track.clips) {
                    if (!clip.audio || m_position < clip.start || m_position - clip.start >= clip.length) continue;
                    const double source = clip.sourceOffset + double(m_position - clip.start) * clip.audio->sampleRate / m_sampleRate;
                    if (source < 0 || source >= clip.audio->frames) continue;
                    const auto at = static_cast<std::size_t>(source);
                    for (int c = 0; c < io.output.channelCount; ++c) {
                        if (clip.audio->channels.empty()) continue;
                        const auto& data = clip.audio->channels[std::min<std::size_t>(c, clip.audio->channels.size()-1)];
                        if (at >= data.size()) continue;
                        const float next = at + 1 < data.size() ? data[at+1] : data[at];
                        io.output.channel(c)[i] += (data[at] + float(source - at) * (next-data[at])) * clip.gain * track.gain;
                    }
                }
            }
            if (m_position < INT64_MAX / 2) ++m_position;
        }
        for (int c = 0; c < io.output.channelCount; ++c) {
            float& sample = io.output.channel(c)[i];
            sample = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
        }
    }
}
