#include "plugins/compressor/mixcompressor.h"
#include "plugins/dynamics/gaincomputer.h"

#include <algorithm>
#include <cmath>

MixCompressor::MixCompressor() : MixEffect(mixfx::Compressor) {}

void MixCompressor::prepareEffect(double rate, int)
{
    for (auto& s : m_smoothers) s.prepare(rate, 20.0f);
    m_rmsCoefficient = dynamics::coefficient(rate, 10.0f);
    m_heldCoefficient = dynamics::coefficient(rate, 1000.0f);
    m_lowCutHz = -1.0f;
}

void MixCompressor::resetEffect() noexcept
{
    m_smoothers[0].reset(control(Threshold));
    m_smoothers[1].reset(control(Ratio));
    m_smoothers[2].reset(control(Knee));
    m_smoothers[3].reset(control(Makeup));
    m_smoothers[4].reset(control(Mix));
    for (auto& channel : m_lowCutState) channel = {};
    m_reduction = m_held = m_meanSquare = 0.0f;
    m_lastOut = {};
}

void MixCompressor::processEffect(ProcessContext& context) noexcept
{
    const double rate = sampleRate();
    const int frames = context.audio.frames;
    const int channels = std::min(context.audio.channelCount, 2);
    const int style = int(control(Style));
    const float attack = dynamics::coefficient(rate, control(Attack));
    const float releaseMs = control(Release);
    const bool autoMakeup = control(AutoMakeup) >= 0.5f;

    const float lowCutHz = control(LowCut);
    if (lowCutHz != m_lowCutHz) {   // a new detector filter only when the control moves
        m_lowCutHz = lowCutHz;
        eqband::Settings band;
        band.shape = eqband::Shape::LowCut;
        band.frequencyHz = lowCutHz;
        band.slope = eqband::Slope::Db12;
        m_lowCut = eqband::design(eqband::sanitize(band, rate), rate);
    }
    const auto detectorFilter = [&](int c, float x) {
        double y = x;
        for (int k = 0; k < m_lowCut.sectionCount; ++k)
            y = biquad::process(m_lowCut.sections[std::size_t(k)], m_lowCutState[std::size_t(c)][std::size_t(k)], y);
        return float(y);
    };

    float blockMax = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float threshold = m_smoothers[0].next(control(Threshold));
        const float ratio = m_smoothers[1].next(control(Ratio));
        const float knee = m_smoothers[2].next(control(Knee));
        const float makeup = m_smoothers[3].next(control(Makeup));
        const float mix = m_smoothers[4].next(control(Mix)) * 0.01f;

        float in[2];
        for (int c = 0; c < 2; ++c) in[c] = context.audio.channel(std::min(c, channels - 1))[i];
        // Punchy listens to its own output (feedback); the others to the input.
        float detect = 0.0f;
        for (int c = 0; c < channels; ++c)
            detect = std::max(detect, std::abs(detectorFilter(c, style == 2 ? m_lastOut[std::size_t(c)] : in[c])));

        float levelDb;
        if (style == 1) {
            m_meanSquare += m_rmsCoefficient * (detect * detect - m_meanSquare);
            // x2: a sine's mean square is half its peak squared, so the threshold reads the same
            levelDb = 10.0f * std::log10(std::max(2.0f * m_meanSquare, 1e-18f));
        } else {
            levelDb = dynamics::toDb(detect);
        }

        const bool feedback = style == 2;
        const float slope = feedback ? dynamics::feedbackSlope(ratio) : dynamics::feedForwardSlope(ratio);
        const float target = dynamics::reductionDb(levelDb, threshold, slope, knee);
        float release = releaseMs;
        if (style == 1) {   // held down longer -> lets go slower, up to 3x
            m_held += m_heldCoefficient * (m_reduction - m_held);
            release *= 1.0f + 2.0f * std::clamp(m_held / 6.0f, 0.0f, 1.0f);
        }
        float step = target > m_reduction ? attack : dynamics::coefficient(rate, release);
        // The feedback loop gains (1 + slope) per sample: keep the step below 1 / (1 + slope) so
        // it settles instead of ringing.
        if (feedback) step = std::min(step, 0.5f / (1.0f + slope));
        m_reduction += step * (target - m_reduction);
        blockMax = std::max(blockMax, m_reduction);

        // Auto makeup: half of what a full-scale signal loses.
        const float automatic = autoMakeup ? -threshold * (1.0f - 1.0f / std::max(ratio, 1.0f)) * 0.5f : 0.0f;
        const float reduced = dynamics::fromDb(-m_reduction);
        const float gain = reduced * dynamics::fromDb(makeup + automatic);
        for (int c = 0; c < channels; ++c) {
            const float wet = in[c] * gain;
            m_lastOut[std::size_t(c)] = in[c] * reduced;   // Punchy's detector: before makeup
            context.audio.channel(c)[i] = in[c] + mix * (wet - in[c]);
        }
    }
    publishReductionDb(blockMax);
}
