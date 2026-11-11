#include "plugins/pitchmonitor/pitchmonitor.h"

#include <algorithm>

PitchMonitor::PitchMonitor() : MixEffect(mixfx::PitchMonitor) {}

void PitchMonitor::prepareEffect(double sampleRate, int maxFrames)
{
    m_tracker.prepare(sampleRate);
    m_mono.assign(std::size_t(std::max(maxFrames, 1)), 0.0f);
    m_pushed = 0;
}

void PitchMonitor::resetEffect() noexcept
{
    m_tracker.reset();
    m_pushed = 0;
}

void PitchMonitor::processEffect(ProcessContext& context) noexcept
{
    const bool listening = m_listening.load(std::memory_order_relaxed);
    if (listening != m_wasListening) {                     // start each listen from a clean window
        m_wasListening = listening;
        resetEffect();
    }
    const auto& audio = context.audio;
    if (!listening || audio.frames <= 0 || audio.channelCount <= 0) return;
    const int channels = std::min(audio.channelCount, 2);
    const std::int64_t blockStart = m_pushed;
    for (int start = 0; start < audio.frames;) {
        const int frames = std::min(audio.frames - start, int(m_mono.size()));
        for (int i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += audio.channel(c)[start + i];
            m_mono[std::size_t(i)] = sum / float(channels);
        }
        m_tracker.push(m_mono.data(), frames, [&](const pitch::Frame& f) {
            m_readings.push({context.samplePosition + (f.sample - blockStart), f.hz, f.clarity, f.levelDb, context.playing});
        });
        m_pushed += frames;
        start += frames;
    }
}
