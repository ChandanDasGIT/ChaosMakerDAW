#include "overdrive.h"

#include <algorithm>
#include <cmath>

Overdrive::Overdrive()
    : m_driveDb(addParameter("drive", "Drive", 0.0f, 30.0f, 12.0f, "dB")),
      m_tone(addParameter("tone", "Tone", 0.0f, 1.0f, 0.62f)),
      m_levelDb(addParameter("level", "Level", -24.0f, 12.0f, -6.0f, "dB"))
{
}

void Overdrive::prepare(double sampleRate, int)
{
    m_sampleRate = std::max(sampleRate, 1000.0);
    for (auto& smoother : m_smoothers) smoother.prepare(m_sampleRate, 15.0f);
    reset();
}

void Overdrive::reset() noexcept
{
    m_smoothers[0].reset(m_driveDb.get());
    m_smoothers[1].reset(m_tone.get());
    m_smoothers[2].reset(m_levelDb.get());
    for (auto& oversampler : m_oversamplers) oversampler.reset();
    m_toneState.fill(0.0f);
}

float Overdrive::shape(float input, void* userData) noexcept
{
    const auto& state = *static_cast<const ShapeState*>(userData);
    return std::tanh(input * state.drive);
}

void Overdrive::process(ProcessContext& context) noexcept
{
    const int channels = std::min(context.audio.channelCount, 2);
    for (int frame = 0; frame < context.audio.frames; ++frame) {
        const float drive = std::pow(10.0f, m_smoothers[0].next(m_driveDb.get()) / 20.0f);
        const float tone = m_smoothers[1].next(m_tone.get());
        const float level = std::pow(10.0f, m_smoothers[2].next(m_levelDb.get()) / 20.0f);
        const float cutoff = 700.0f * std::pow(18.0f, tone);
        const float toneCoefficient = 1.0f - std::exp(-2.0f * 3.14159265358979323846f
                                                       * cutoff / static_cast<float>(m_sampleRate));
        ShapeState state{drive};
        for (int channel = 0; channel < channels; ++channel) {
            float* samples = context.audio.channel(channel);
            const float saturated = m_oversamplers[static_cast<std::size_t>(channel)]
                .process(samples[frame], channel, &Overdrive::shape, &state);
            float& toneState = m_toneState[static_cast<std::size_t>(channel)];
            toneState += toneCoefficient * (saturated - toneState);
            samples[frame] = toneState * level;
        }
    }
}
