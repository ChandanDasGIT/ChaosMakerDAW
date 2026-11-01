#include "guitarsuite.h"

#include <algorithm>

GuitarSuite::GuitarSuite(std::vector<std::shared_ptr<PedalSlot>> slots)
    : m_slots(std::move(slots))
{
}

void GuitarSuite::prepare(double sampleRate, int maxFrames)
{
    m_sampleRate = sampleRate;
    m_maxFrames = std::max(maxFrames, 1);
    m_runtime.clear();
    m_runtime.reserve(m_slots.size());
    for (const auto& slot : m_slots) {
        if (slot->preparedSampleRate == 0.0) {
            slot->processor->prepare(sampleRate, maxFrames);
            slot->preparedSampleRate = sampleRate;
            slot->preparedMaxFrames = maxFrames;
        }
        RuntimeSlot runtime;
        runtime.state = slot;
        runtime.wet.prepare(sampleRate, 8.0f);
        runtime.wet.reset(slot->bypassed.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
        runtime.dry.resize(static_cast<std::size_t>(m_maxFrames * m_maxChannels));
        m_runtime.push_back(std::move(runtime));
    }
}

void GuitarSuite::reset() noexcept
{
    for (auto& slot : m_runtime) {
        slot.state->processor->reset();
        slot.wet.reset(slot.state->bypassed.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
        std::fill(slot.dry.begin(), slot.dry.end(), 0.0f);
    }
}

void GuitarSuite::analyseInput(ProcessContext& context) noexcept
{
    for (auto& slot : m_runtime) {
        if (!slot.state->processor->analysisOnly()) continue;
        if (slot.state->bypassed.load(std::memory_order_relaxed)) slot.state->processor->reset();
        else slot.state->processor->process(context);
    }
}

void GuitarSuite::process(ProcessContext& context) noexcept
{
    const int channels = context.audio.channelCount;
    const int frames = context.audio.frames;
    if (frames <= 0 || frames > m_maxFrames || channels <= 0 || channels > m_maxChannels)
        return;

    for (auto& slot : m_runtime) {
        if (slot.state->processor->analysisOnly()) continue;
        for (int channel = 0; channel < channels; ++channel)
            std::copy_n(context.audio.channel(channel), frames,
                        slot.dry.data() + static_cast<std::size_t>(channel * m_maxFrames));

        slot.state->processor->process(context);
        const float targetWet = slot.state->bypassed.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
        for (int frame = 0; frame < frames; ++frame) {
            const float wet = slot.wet.next(targetWet);
            for (int channel = 0; channel < channels; ++channel) {
                const float dry = slot.dry[static_cast<std::size_t>(channel * m_maxFrames + frame)];
                float& output = context.audio.channel(channel)[frame];
                output = dry * (1.0f - wet) + output * wet;
            }
        }
    }
}
