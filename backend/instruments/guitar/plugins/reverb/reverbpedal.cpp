#include "reverbpedal.h"

#include <algorithm>

namespace {
constexpr std::array<int, 4> CombLengths{1116, 1188, 1277, 1356};
constexpr std::array<int, 2> AllpassLengths{225, 556};
}

ReverbPedal::ReverbPedal()
    : m_room(addParameter("room", "Room", 0.0f, 0.95f, 0.72f)),
      m_damping(addParameter("damping", "Damping", 0.0f, 0.95f, 0.28f)),
      m_mix(addParameter("mix", "Mix", 0.0f, 1.0f, 0.22f))
{
}

void ReverbPedal::prepare(double sampleRate, int)
{
    m_sampleRate = std::max(sampleRate, 1000.0);
    for (int channel = 0; channel < 2; ++channel) {
        const int spread = channel == 0 ? 0 : 23;
        for (std::size_t i = 0; i < CombLengths.size(); ++i)
            m_combs[static_cast<std::size_t>(channel)][i].line.prepare(
                static_cast<int>(CombLengths[i] * m_sampleRate / 44100.0) + spread);
        for (std::size_t i = 0; i < AllpassLengths.size(); ++i)
            m_allpasses[static_cast<std::size_t>(channel)][i].prepare(
                static_cast<int>(AllpassLengths[i] * m_sampleRate / 44100.0) + spread);
    }
    for (auto& smoother : m_smoothers) smoother.prepare(m_sampleRate, 30.0f);
    reset();
}

void ReverbPedal::reset() noexcept
{
    for (auto& channel : m_combs) {
        for (auto& comb : channel) {
            comb.line.reset();
            comb.dampingState = 0.0f;
        }
    }
    for (auto& channel : m_allpasses)
        for (auto& line : channel) line.reset();
    m_smoothers[0].reset(m_room.get());
    m_smoothers[1].reset(m_damping.get());
    m_smoothers[2].reset(m_mix.get());
}

void ReverbPedal::process(ProcessContext& context) noexcept
{
    const int channels = std::min(context.audio.channelCount, 2);
    for (int frame = 0; frame < context.audio.frames; ++frame) {
        const float room = m_smoothers[0].next(m_room.get());
        const float damping = m_smoothers[1].next(m_damping.get());
        const float mix = m_smoothers[2].next(m_mix.get());
        for (int channel = 0; channel < channels; ++channel) {
            float& inputOutput = context.audio.channel(channel)[frame];
            const float dry = inputOutput;
            float wet = 0.0f;
            auto& combs = m_combs[static_cast<std::size_t>(channel)];
            for (auto& comb : combs) {
                const float delayed = comb.line.read(1.0f);
                comb.dampingState = delayed * (1.0f - damping) + comb.dampingState * damping;
                comb.line.write(dry + comb.dampingState * room);
                wet += delayed;
            }
            wet *= 0.25f;
            auto& allpasses = m_allpasses[static_cast<std::size_t>(channel)];
            for (auto& line : allpasses) {
                const float delayed = line.read(1.0f);
                line.write(wet + delayed * 0.5f);
                wet = delayed - wet;
            }
            inputOutput = dry * (1.0f - mix) + wet * mix;
        }
    }
}
