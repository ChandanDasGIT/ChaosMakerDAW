#include "plugins/limiter/limiter.h"
#include "plugins/dynamics/gaincomputer.h"

#include <algorithm>
#include <cmath>

Limiter::Limiter() : MixEffect(mixfx::Limiter) {}

void Limiter::prepareEffect(double rate, int)
{
    m_lookahead = std::max(1, int(std::lround(0.001 * rate)));
    for (auto& d : m_delay) d.prepare(m_lookahead + 4);
    m_window.assign(std::size_t(m_lookahead + 1), 1.0f);
    m_average.assign(std::size_t(m_lookahead), 1.0f);
}

void Limiter::resetEffect() noexcept
{
    for (auto& d : m_delay) d.clear();
    std::fill(m_window.begin(), m_window.end(), 1.0f);
    std::fill(m_average.begin(), m_average.end(), 1.0f);
    m_windowPos = m_averagePos = 0;
    m_averageSum = double(m_lookahead);
    m_gain = 1.0f;
}

void Limiter::processEffect(ProcessContext& context) noexcept
{
    const int frames = context.audio.frames;
    const int channels = std::min(context.audio.channelCount, 2);
    const float drive = dynamics::fromDb(control(Gain));
    const float ceiling = dynamics::fromDb(control(Ceiling));
    const float release = dynamics::coefficient(sampleRate(), control(Release));
    float deepest = 0.0f;
    for (int i = 0; i < frames; ++i) {
        float peak = 0.0f;
        std::array<float, 2> in{};
        for (int c = 0; c < channels; ++c) {
            in[std::size_t(c)] = context.audio.channel(c)[i] * drive;
            peak = std::max(peak, std::abs(in[std::size_t(c)]));
        }
        m_window[std::size_t(m_windowPos)] = peak > ceiling ? ceiling / peak : 1.0f;
        m_windowPos = (m_windowPos + 1) % int(m_window.size());
        float held = 1.0f;
        for (float g : m_window) held = std::min(held, g);
        m_averageSum += double(held) - double(m_average[std::size_t(m_averagePos)]);
        m_average[std::size_t(m_averagePos)] = held;
        m_averagePos = (m_averagePos + 1) % m_lookahead;
        const float smooth = std::min(1.0f, float(m_averageSum / double(m_lookahead)));
        m_gain = smooth < m_gain ? smooth : m_gain + release * (smooth - m_gain);
        deepest = std::max(deepest, -dynamics::toDb(m_gain));
        for (int c = 0; c < channels; ++c) {
            m_delay[std::size_t(c)].push(in[std::size_t(c)]);
            const float delayed = m_delay[std::size_t(c)].read(m_lookahead + 1);
            context.audio.channel(c)[i] = std::clamp(delayed * m_gain, -ceiling, ceiling);
        }
    }
    publishReductionDb(deepest);
}
