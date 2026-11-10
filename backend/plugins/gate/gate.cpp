#include "plugins/gate/gate.h"
#include "plugins/dynamics/gaincomputer.h"

#include <algorithm>
#include <cmath>

Gate::Gate() : MixEffect(mixfx::Gate) {}

void Gate::prepareEffect(double rate, int)
{
    m_envelopeRelease = dynamics::coefficient(rate, 10.0f);
}

void Gate::resetEffect() noexcept
{
    m_envelope = 0.0f;
    m_reduction = control(Range);   // starts closed: nothing has been heard yet
    m_holdLeft = 0;
    m_open = false;
}

void Gate::processEffect(ProcessContext& context) noexcept
{
    const double rate = sampleRate();
    const int frames = context.audio.frames;
    const int channels = std::min(context.audio.channelCount, 2);
    const float threshold = control(Threshold);
    const float range = control(Range);
    const float attack = dynamics::coefficient(rate, control(Attack));
    const float release = dynamics::coefficient(rate, control(Release));
    const int hold = int(control(Hold) * 0.001 * rate);
    float deepest = 0.0f;
    for (int i = 0; i < frames; ++i) {
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) peak = std::max(peak, std::abs(context.audio.channel(c)[i]));
        // A peak follower: instant up, 10 ms down, so the gate hears a waveform, not its zero crossings.
        m_envelope = peak > m_envelope ? peak : m_envelope + m_envelopeRelease * (peak - m_envelope);
        const float level = dynamics::toDb(m_envelope);
        if (level >= threshold) { m_open = true; m_holdLeft = hold; }
        else if (m_open && level < threshold - 4.0f) {
            if (m_holdLeft > 0) --m_holdLeft;
            else m_open = false;
        }
        const float target = m_open ? 0.0f : range;
        m_reduction += (target < m_reduction ? attack : release) * (target - m_reduction);
        deepest = std::max(deepest, m_reduction);
        const float gain = dynamics::fromDb(-m_reduction);
        for (int c = 0; c < channels; ++c) context.audio.channel(c)[i] *= gain;
    }
    publishReductionDb(deepest);
}
