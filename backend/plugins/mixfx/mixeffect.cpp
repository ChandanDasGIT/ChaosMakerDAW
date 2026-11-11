#include "plugins/mixfx/mixeffect.h"

#include <algorithm>
#include <cstring>

MixEffect::MixEffect(std::string_view type) : m_spec(mixfx::find(type))
{
    // An unknown type is a programming error: fall back to the first spec so nothing dangles.
    if (!m_spec) m_spec = &mixfx::all().front();
    for (const auto& c : m_spec->controls)
        m_controls.push_back(&addParameter(std::string(c.id), std::string(c.name), c.min, c.max, c.def, std::string(c.unit)));
    m_bypass = &addParameter("bypass", "Bypass", 0.0f, 1.0f, 0.0f);
}

void MixEffect::prepare(double sampleRate, int maxFrames)
{
    m_sampleRate = std::max(sampleRate, 1000.0);
    for (auto& dry : m_dry) dry.assign(std::size_t(std::max(maxFrames, 1)), 0.0f);
    for (auto& line : m_delayLine) line.assign(std::size_t(std::max(latencySamples(), 0)), 0.0f);
    m_delayPos = 0;
    m_wetStep = float(1.0 / (0.020 * m_sampleRate));
    prepareEffect(m_sampleRate, std::max(maxFrames, 1));
    reset();
}

void MixEffect::reset() noexcept
{
    m_wet = m_bypass->get() >= 0.5f ? 0.0f : 1.0f;
    publishReductionDb(0.0f);
    for (auto& line : m_delayLine) std::fill(line.begin(), line.end(), 0.0f);
    resetEffect();
}

void MixEffect::apply(const mixfx::Settings& settings) noexcept
{
    for (std::size_t i = 0; i < m_controls.size(); ++i)
        m_controls[i]->set(mixfx::value(*m_spec, settings, m_spec->controls[i].id));
    m_bypass->set(settings.bypassed ? 1.0f : 0.0f);
    applyExtra(settings);
}

Parameter* MixEffect::parameter(std::string_view id) noexcept
{
    for (const auto& p : parameters()) if (p->id == id) return p.get();
    return nullptr;
}

void MixEffect::process(ProcessContext& context) noexcept
{
    const float target = m_bypass->get() >= 0.5f ? 0.0f : 1.0f;
    const int frames = context.audio.frames;
    const int channels = std::min(context.audio.channelCount, 2);
    if (frames <= 0 || channels <= 0) return;
    // The dry signal, delayed like the effect's output (effects with a delay only).
    const std::size_t delay = m_delayLine[0].size();
    const bool delayed = delay > 0 && std::size_t(frames) <= m_dry[0].size();
    if (delayed) {
        for (int c = 0; c < channels; ++c) {
            std::size_t pos = m_delayPos;
            float* io = context.audio.channel(c);
            for (int i = 0; i < frames; ++i) {
                m_dry[c][std::size_t(i)] = m_delayLine[c][pos];
                m_delayLine[c][pos] = io[i];
                if (++pos == delay) pos = 0;
            }
        }
        m_delayPos = (m_delayPos + std::size_t(frames)) % delay;
    }
    if (m_wet == 0.0f && target == 0.0f) {
        publishReductionDb(0.0f);
        if (delayed) for (int c = 0; c < channels; ++c)
            std::memcpy(context.audio.channel(c), m_dry[c].data(), sizeof(float) * std::size_t(frames));
        return;
    }
    if (m_wet == 0.0f) resetEffect();   // switched back on: start from silence, not an old tail
    if (m_wet == target && m_wet == 1.0f) { processEffect(context); return; }

    // Crossfading: keep the dry block (blocks larger than prepared skip the fade).
    if (std::size_t(frames) > m_dry[0].size()) {
        m_wet = target;
        if (target == 1.0f) processEffect(context);
        return;
    }
    if (!delayed)
        for (int c = 0; c < channels; ++c) std::memcpy(m_dry[c].data(), context.audio.channel(c), sizeof(float) * std::size_t(frames));
    processEffect(context);
    for (int i = 0; i < frames; ++i) {
        m_wet = target > m_wet ? std::min(target, m_wet + m_wetStep) : std::max(target, m_wet - m_wetStep);
        for (int c = 0; c < channels; ++c) {
            float& out = context.audio.channel(c)[i];
            out = m_dry[c][std::size_t(i)] + m_wet * (out - m_dry[c][std::size_t(i)]);
        }
    }
}
