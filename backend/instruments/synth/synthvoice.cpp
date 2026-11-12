#include "instruments/synth/synthvoice.h"

#include <algorithm>
#include <cmath>

namespace synth {

namespace {

constexpr double FadeSeconds = 0.004;               // a taken voice
constexpr double MixScale = 0.5;                    // two full oscillators plus sub and noise

double keyHz(double key) noexcept { return 440.0 * std::exp2((key - 69.0) / 12.0); }

} // namespace

void Voice::start(int key, int velocity, bool retrigger, double glideSeconds, double rate) noexcept
{
    const bool sounding = active();
    m_key = key;
    m_velocity = std::clamp(velocity, 1, 127) / 127.0;
    m_targetPitch = key;
    if (sounding && glideSeconds > 0.0) {
        m_glideCoef = fallCoefficient(glideSeconds, rate);
    } else {
        m_glideCoef = 0.0;
        m_pitch = key;
    }
    if (!sounding) {                                // a fresh voice: oscillators from the top
        m_osc[0].reset(0.0);
        m_osc[1].reset(0.25);                       // a little apart, so equal pitches don't cancel
        m_sub.reset(0.0);
        m_filter.reset();
        m_amp.reset();
        m_filterEnv.reset();
        m_lfoFade = 0.0;
    }
    if (retrigger || !sounding) {
        m_amp.gateOn();
        m_filterEnv.gateOn();
        m_lfoFade = 0.0;
    }
}

void Voice::release() noexcept
{
    m_amp.gateOff();
    m_filterEnv.gateOff();
}

void Voice::fadeOut(double rate) noexcept
{
    m_amp.fadeOut(FadeSeconds, rate);
    m_filterEnv.gateOff();
}

void Voice::stop() noexcept
{
    m_amp.reset();
    m_filterEnv.reset();
    m_filter.reset();
    m_key = -1;
}

float Voice::noise() noexcept
{
    m_seed ^= m_seed << 13; m_seed ^= m_seed >> 17; m_seed ^= m_seed << 5;
    return float(m_seed) * (2.0f / 4294967296.0f) - 1.0f;
}

void Voice::updateFilter(const VoiceControls& c, float filterEnv) noexcept
{
    const auto& f = c.settings->filter;
    const double velocityScale = 1.0 - f.velocity * (1.0 - m_velocity);
    const double octaves = f.keyTrack * (m_pitch - 60.0) / 12.0
                         + f.envOctaves * filterEnv * velocityScale
                         + c.lfoOctaves * m_lfoFade;
    m_filter.set(f.mode, f.cutoffHz * std::exp2(octaves), f.resonance, c.sampleRate);
}

void Voice::render(float* out, int frames, const VoiceControls& c) noexcept
{
    if (!active() || !c.settings) return;
    const Settings& s = *c.settings;
    const double rate = c.sampleRate;

    m_amp.configure(s.ampEnv, rate);
    m_filterEnv.configure(s.filterEnv, rate);
    m_lfoFadeStep = s.lfo.delaySeconds > 0.0 ? 1.0 / (s.lfo.delaySeconds * rate) : 1.0;

    const double gain = 1.0 - s.ampVelocity * (1.0 - m_velocity);
    const double drive = 1.0 + 6.0 * s.filter.drive;
    const double driveNorm = 1.0 / std::tanh(drive);
    const float noiseLevel = float(s.noiseLevel);
    const float subLevel = float(s.subLevel);

    // Pitch and pulse width move once per block (32 samples at most): smooth enough for glide,
    // vibrato and bends. The glide is stepped by the frames the block spans.
    const double glide = m_glideCoef > 0.0 ? std::pow(m_glideCoef, frames) : 0.0;
    m_pitch = m_targetPitch + (m_pitch - m_targetPitch) * glide;
    const double fade = m_lfoFade;
    m_lfoFade = std::min(1.0, m_lfoFade + m_lfoFadeStep * frames);

    const double basePitch = m_pitch + c.bendSemitones + c.lfoSemitones * fade;
    double increments[OscillatorCount]{};
    double widths[OscillatorCount]{};
    for (int i = 0; i < OscillatorCount; ++i) {
        const auto& o = s.osc[std::size_t(i)];
        const double key = basePitch + 12.0 * o.octave + o.semitones + o.fineCents / 100.0;
        increments[i] = std::min(keyHz(key) / rate, 0.49);
        widths[i] = std::clamp(o.pulseWidth + c.lfoPulseWidth * fade, MinPulseWidth, MaxPulseWidth);
    }
    const auto& first = s.osc[0];
    const double subKey = basePitch + 12.0 * first.octave + first.semitones - 12.0;
    const double subIncrement = std::min(keyHz(subKey) / rate, 0.49);
    const Wave subWave = s.subWave == SubWave::Square ? Wave::Pulse : Wave::Sine;
    const float volume = float(gain * (1.0 - c.lfoVolumeDip * fade));

    for (int n = 0; n < frames; ++n) {
        const float filterEnv = m_filterEnv.next();
        if (n % FilterStep == 0) updateFilter(c, filterEnv);

        float x = 0.0f;
        for (int i = 0; i < OscillatorCount; ++i) {
            const auto& o = s.osc[std::size_t(i)];
            if (o.on && o.level > 0.0)
                x += float(o.level) * m_osc[i].next(o.wave, increments[i], widths[i]);
        }
        if (subLevel > 0.0f) x += subLevel * m_sub.next(subWave, subIncrement, 0.5);
        if (noiseLevel > 0.0f) x += noiseLevel * noise();

        double y = x * MixScale;
        if (s.filter.drive > 0.0) y = std::tanh(y * drive) * driveNorm;
        y = m_filter.process(float(y));
        out[n] += float(y) * volume * m_amp.next();
        if (m_amp.idle()) { m_key = -1; break; }
    }
}

} // namespace synth
