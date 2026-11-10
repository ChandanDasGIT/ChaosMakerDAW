#include "plugins/saturation/saturation.h"
#include "plugins/dynamics/gaincomputer.h"

#include <algorithm>
#include <cmath>

Saturation::Saturation() : MixEffect(mixfx::Saturation) {}

float Saturation::shape(int type, float x) noexcept
{
    switch (type) {
    case 1: {   // Tube: a small square term before a soft curve: even harmonics, warmer
        const float biased = x + 0.18f * x * x - 0.18f;   // the bias keeps silence at 0 after the DC term
        return std::tanh(biased) + std::tanh(0.18f);
    }
    case 2: {   // Clip: straight until near the top, then a short knee into a flat top
        const float a = std::abs(x);
        const float y = a < 0.8f ? a : a < 1.4f ? 0.8f + (a - 0.8f) - (a - 0.8f) * (a - 0.8f) / 1.2f : 1.1f;
        return std::copysign(y, x);
    }
    default:    // Tape: a smooth, symmetric curve
        return std::tanh(x);
    }
}

void Saturation::prepareEffect(double rate, int)
{
    for (auto& d : m_dry) d.prepare(Latency + 4);
    for (auto* s : {&m_drive, &m_tone, &m_output, &m_mix}) s->prepare(rate, 20.0f);
}

void Saturation::designTilt(float tone) noexcept
{
    eqband::Settings tilt;
    tilt.shape = eqband::Shape::TiltShelf;
    tilt.frequencyHz = 1000.0;
    tilt.gainDb = (tone - 0.5f) * 24.0f;   // the shelf's span: +-6 dB at each end
    m_tilt = eqband::design(eqband::sanitize(tilt, sampleRate()), sampleRate());
    m_tiltTone = tone;
}

void Saturation::resetEffect() noexcept
{
    m_oversampler.reset();
    for (auto& d : m_dry) d.clear();
    for (auto& channel : m_tiltState) channel = {};
    m_tiltCountdown = 0;
    designTilt(control(Tone) * 0.01f);
    m_drive.reset(control(Drive));
    m_tone.reset(control(Tone) * 0.01f);
    m_output.reset(control(Output));
    m_mix.reset(control(Mix) * 0.01f);
}

void Saturation::processEffect(ProcessContext& context) noexcept
{
    const int frames = context.audio.frames;
    const int channels = std::min(context.audio.channelCount, 2);
    ShapeState state{int(control(Type)), 1.0f};
    const auto curve = [](float x, void* s) {
        const auto* st = static_cast<ShapeState*>(s);
        return shape(st->type, x * st->drive);
    };
    for (int i = 0; i < frames; ++i) {
        const float driveDb = m_drive.next(control(Drive));
        state.drive = dynamics::fromDb(driveDb);
        // The curve's own gain at a typical level (-12 dBFS) comes back off, so turning Drive up
        // adds colour, not level, whatever the type.
        constexpr float reference = 0.25f;
        const float through = std::max(1e-6f, std::abs(shape(state.type, reference * state.drive)));
        const float makeup = (reference / through) * dynamics::fromDb(m_output.next(control(Output)));
        const float tone = m_tone.next(control(Tone) * 0.01f);
        if (--m_tiltCountdown <= 0) {   // follow a moving Tone every 32 samples
            m_tiltCountdown = 32;
            if (std::abs(tone - m_tiltTone) > 0.002f) designTilt(tone);
        }
        const float mix = m_mix.next(control(Mix) * 0.01f);
        for (int c = 0; c < channels; ++c) {
            float& sample = context.audio.channel(c)[i];
            const float in = sample;
            float wet = m_oversampler.process(in, c, curve, &state) * makeup;
            // Tone: 50 % flat; lower, darker; higher, brighter.
            double tilted = wet;
            for (int k = 0; k < m_tilt.sectionCount; ++k)
                tilted = biquad::process(m_tilt.sections[std::size_t(k)], m_tiltState[std::size_t(c)][std::size_t(k)], tilted);
            wet = float(tilted);
            m_dry[std::size_t(c)].push(in);
            const float dry = m_dry[std::size_t(c)].read(Latency + 1);
            sample = dry + mix * (wet - dry);
        }
    }
}
