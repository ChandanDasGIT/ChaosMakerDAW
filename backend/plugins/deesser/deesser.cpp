#include "plugins/deesser/deesser.h"
#include "plugins/dynamics/gaincomputer.h"

#include <algorithm>
#include <cmath>

DeEsser::DeEsser() : MixEffect(mixfx::DeEsser) {}

void DeEsser::prepareEffect(double rate, int)
{
    for (auto& s : m_smoothers) s.prepare(rate, 20.0f);
    // Fast enough to catch an "s" as it starts, slow enough to let go without a flutter.
    m_attack = dynamics::coefficient(rate, 0.5f);
    m_release = dynamics::coefficient(rate, 60.0f);
    m_frequency = -1.0f;
}

void DeEsser::designShelf(float cutDb) noexcept
{
    eqband::Settings shelf;
    shelf.shape = eqband::Shape::HighShelf;
    shelf.frequencyHz = m_frequency;
    shelf.gainDb = -cutDb;
    shelf.slope = eqband::Slope::Db24;
    m_shelf = eqband::design(eqband::sanitize(shelf, sampleRate()), sampleRate());
    m_shelfDb = cutDb;
}

void DeEsser::resetEffect() noexcept
{
    m_smoothers[0].reset(control(Threshold));
    m_smoothers[1].reset(control(Range));
    for (auto& channel : m_state) channel = {};
    for (auto& channel : m_shelfState) channel = {};
    m_reduction = 0.0f;
    m_shelfCountdown = 0;
    if (m_frequency > 0.0f) designShelf(0.0f);
}

void DeEsser::processEffect(ProcessContext& context) noexcept
{
    const double rate = sampleRate();
    const int frames = context.audio.frames;
    const int channels = std::min(context.audio.channelCount, 2);
    const float frequency = control(Frequency);
    if (frequency != m_frequency) {
        m_frequency = frequency;
        eqband::Settings band;
        band.shape = eqband::Shape::LowCut;   // a high-pass: keeps what is above the frequency
        band.frequencyHz = frequency;
        band.slope = eqband::Slope::Db12;
        m_highPass = eqband::design(eqband::sanitize(band, rate), rate);
        designShelf(m_reduction);
    }
    const bool listen = control(Listen) >= 0.5f;
    const bool wide = int(control(Mode)) == 1;
    const float slope = dynamics::feedForwardSlope(8.0f);   // firm, but with a soft knee

    float blockMax = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float threshold = m_smoothers[0].next(control(Threshold));
        const float range = m_smoothers[1].next(control(Range));
        float band[2]{};
        float level = 0.0f;
        for (int c = 0; c < channels; ++c) {
            double y = context.audio.channel(c)[i];
            for (int k = 0; k < m_highPass.sectionCount; ++k)
                y = biquad::process(m_highPass.sections[std::size_t(k)], m_state[std::size_t(c)][std::size_t(k)], y);
            band[c] = float(y);
            level = std::max(level, std::abs(band[c]));
        }
        const float target = std::min(range, dynamics::reductionDb(dynamics::toDb(level), threshold, slope, 6.0f));
        m_reduction += (target > m_reduction ? m_attack : m_release) * (target - m_reduction);
        blockMax = std::max(blockMax, m_reduction);
        const float gain = dynamics::fromDb(-m_reduction);
        const bool split = !listen && !wide;
        // The shelf follows the reduction, redesigned at most every 16 samples and only when
        // the cut has moved.
        if (split && --m_shelfCountdown <= 0) {
            m_shelfCountdown = 16;
            if (std::abs(m_reduction - m_shelfDb) > 0.05f) designShelf(m_reduction);
        }
        for (int c = 0; c < channels; ++c) {
            float& sample = context.audio.channel(c)[i];
            if (listen) sample = band[c] * gain;
            else if (wide) sample *= gain;
            else {
                double y = sample;
                for (int k = 0; k < m_shelf.sectionCount; ++k)
                    y = biquad::process(m_shelf.sections[std::size_t(k)], m_shelfState[std::size_t(c)][std::size_t(k)], y);
                sample = float(y);
            }
        }
    }
    publishReductionDb(blockMax);
}
