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
    const double oversampled = m_sampleRate * 2;
    for (auto& s : m_shape) {
        s.highpassCoefficient = float(std::exp(-2.0 * 3.14159265358979 * 720.0 / oversampled));
        s.stage.positiveKnee = 1.0f;     // a diode pair: symmetric, soft
        s.stage.negativeKnee = 1.0f;
        s.stage.conduction = 0.8f;
        s.stage.biasDepth = 0.3f;
        s.stage.prepare(oversampled, 2.0f, 40.0f);
    }
    m_rolloff.set({eqband::Shape::HighCut, 5500.0, 0.0, eqband::DefaultQ, eqband::Slope::Db12}, m_sampleRate);
    reset();
}

void Overdrive::reset() noexcept
{
    m_smoothers[0].reset(m_driveDb.get());
    m_smoothers[1].reset(m_tone.get());
    m_smoothers[2].reset(m_levelDb.get());
    for (auto& oversampler : m_oversamplers) oversampler.reset();
    for (auto& s : m_shape) { s.previousInput = s.highpass = 0.0f; s.stage.reset(); }
    m_rolloff.reset();
    m_toneShelf.reset();
    m_frameCounter = 0;
    m_appliedTone = -1.0f;
    refresh(m_tone.get());
}

void Overdrive::refresh(float tone) noexcept
{
    if (tone == m_appliedTone) return;
    m_appliedTone = tone;
    m_toneShelf.set({eqband::Shape::HighShelf, 1200.0, -10.0 + 16.0 * tone, eqband::DefaultQ, eqband::Slope::Db6},
                    m_sampleRate);
}

// Clean signal plus the clipped upper part of it (the op-amp sums them).
float Overdrive::shape(float input, void* userData) noexcept
{
    auto& s = *static_cast<ShapeState*>(userData);
    s.highpass = s.highpassCoefficient * (s.highpass + input - s.previousInput);
    s.previousInput = input;
    return input + 0.5f * s.stage.process(s.highpass * s.gain);
}

void Overdrive::process(ProcessContext& context) noexcept
{
    const int channels = std::min(context.audio.channelCount, 2);
    for (int frame = 0; frame < context.audio.frames; ++frame) {
        const float gain = std::pow(10.0f, (m_smoothers[0].next(m_driveDb.get()) + 6.0f) / 20.0f);
        const float tone = m_smoothers[1].next(m_tone.get());
        const float level = std::pow(10.0f, m_smoothers[2].next(m_levelDb.get()) / 20.0f);
        if (m_frameCounter++ % RefreshFrames == 0) refresh(tone);
        for (int channel = 0; channel < channels; ++channel) {
            float* samples = context.audio.channel(channel);
            auto& state = m_shape[static_cast<std::size_t>(channel)];
            state.gain = gain;
            float y = m_oversamplers[static_cast<std::size_t>(channel)].process(samples[frame], channel, &Overdrive::shape, &state);
            y = m_rolloff.run(channel, y);
            y = m_toneShelf.run(channel, y);
            samples[frame] = y * level;
        }
    }
}
