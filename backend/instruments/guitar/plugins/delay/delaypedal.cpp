#include "delaypedal.h"

#include <algorithm>

DelayPedal::DelayPedal()
    : m_timeMs(addParameter("time", "Time", 10.0f, 2000.0f, 320.0f, "ms")),
      m_feedback(addParameter("feedback", "Feedback", 0.0f, 0.92f, 0.34f)),
      m_mix(addParameter("mix", "Mix", 0.0f, 1.0f, 0.25f))
{
}

void DelayPedal::prepare(double sampleRate, int)
{
    m_sampleRate = std::max(sampleRate, 1000.0);
    const int capacity = static_cast<int>(m_sampleRate * 2.0) + 2;
    for (auto& line : m_delayLines) line.prepare(capacity);
    for (auto& smoother : m_smoothers) smoother.prepare(m_sampleRate, 20.0f);
    reset();
}

void DelayPedal::reset() noexcept
{
    for (auto& line : m_delayLines) line.reset();
    m_smoothers[0].reset(m_timeMs.get());
    m_smoothers[1].reset(m_feedback.get());
    m_smoothers[2].reset(m_mix.get());
}

void DelayPedal::process(ProcessContext& context) noexcept
{
    const int channels = std::min(context.audio.channelCount, 2);
    for (int frame = 0; frame < context.audio.frames; ++frame) {
        const float delaySamples = m_smoothers[0].next(m_timeMs.get())
                                   * static_cast<float>(m_sampleRate * 0.001);
        const float feedback = m_smoothers[1].next(m_feedback.get());
        const float mix = m_smoothers[2].next(m_mix.get());
        for (int channel = 0; channel < channels; ++channel) {
            float& sample = context.audio.channel(channel)[frame];
            auto& line = m_delayLines[static_cast<std::size_t>(channel)];
            const float delayed = line.read(delaySamples);
            line.write(sample + delayed * feedback);
            sample = sample * (1.0f - mix) + delayed * mix;
        }
    }
}
