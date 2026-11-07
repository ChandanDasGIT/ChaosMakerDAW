#include "instruments/sampler/samplervoice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr float silence = 0.001f;                          // -60 dB: where decay and release end
constexpr float fadeSeconds = 0.005f;                      // fadeOut(): short, but no click

// The per-frame multiplier that shrinks a distance to `silence` of itself in `seconds`.
float fallCoefficient(float seconds, double rate) noexcept
{
    const double frames = double(seconds) * rate;
    if (!(frames >= 1.0)) return 0.0f;                     // zero, negative or NaN: at once
    return float(std::exp(std::log(double(silence)) / frames));
}

} // namespace

void SamplerVoice::start(const SampleZone& zone, int key, int velocity, double outputRate) noexcept
{
    m_zone = &zone;
    m_key = key;
    m_position = 0.0;
    if (!(outputRate > 0.0)) outputRate = 48000.0;

    const auto& audio = *zone.audio;
    const double sourceRate = audio.sampleRate > 0.0 ? audio.sampleRate : outputRate;
    const double semitones = double(key - zone.rootKey) + double(zone.tuneCents) / 100.0;
    m_increment = std::exp2(semitones / 12.0) * sourceRate / outputRate;

    m_looping = zone.looping && zone.loopStart >= 0 && zone.loopEnd > zone.loopStart
             && zone.loopEnd < audio.frames;
    m_loopStart = zone.loopStart;
    m_loopEnd = zone.loopEnd;

    const float vel = float(std::clamp(velocity, 0, 127)) / 127.0f;
    const float gain = vel * vel * std::pow(10.0f, zone.volumeDb / 20.0f);
    const float pan = std::clamp(std::isfinite(zone.pan) ? zone.pan : 0.0f, -1.0f, 1.0f);
    if (audio.channels.size() >= 2) {                      // balance
        m_gainLeft = gain * std::min(1.0f, 1.0f - pan);
        m_gainRight = gain * std::min(1.0f, 1.0f + pan);
    } else {                                               // equal power, unity in the centre
        const float angle = (pan + 1.0f) * float(std::numbers::pi) / 4.0f;
        m_gainLeft = gain * std::cos(angle) * std::numbers::sqrt2_v<float>;
        m_gainRight = gain * std::sin(angle) * std::numbers::sqrt2_v<float>;
    }

    const double attackFrames = double(zone.attack) * outputRate;
    m_attackStep = attackFrames >= 1.0 ? float(1.0 / attackFrames) : 1.0f;
    m_decayCoef = fallCoefficient(zone.decay, outputRate);
    m_releaseCoef = fallCoefficient(zone.release, outputRate);
    m_fadeCoef = fallCoefficient(fadeSeconds, outputRate);
    m_fading = false;
    m_sustain = std::clamp(zone.sustain, 0.0f, 1.0f);

    m_level = 0.0f;
    m_stage = Stage::Attack;
}

void SamplerVoice::release() noexcept
{
    if (m_stage != Stage::Off) m_stage = Stage::Release;
}

void SamplerVoice::fadeOut() noexcept
{
    if (m_stage == Stage::Off) return;
    m_stage = Stage::Release;
    m_fading = true;
    m_releaseCoef = std::min(m_releaseCoef, m_fadeCoef);   // never slower than the zone's own release
}

void SamplerVoice::stop() noexcept
{
    m_stage = Stage::Off;
    m_level = 0.0f;
    m_zone = nullptr;
    m_key = -1;
    m_fading = false;
}

float SamplerVoice::sampleAt(int channel, std::int64_t index) const noexcept
{
    if (m_looping && index > m_loopEnd)
        index = m_loopStart + (index - m_loopEnd - 1) % (m_loopEnd - m_loopStart + 1);
    const auto& data = m_zone->audio->channels[std::size_t(channel)];
    const std::int64_t frames = std::min<std::int64_t>(m_zone->audio->frames, std::int64_t(data.size()));
    if (index < 0 || index >= frames) return 0.0f;
    return data[std::size_t(index)];
}

// 4-point, 3rd-order Hermite between index and index + 1.
float SamplerVoice::interpolate(int channel, std::int64_t index, float t) const noexcept
{
    const float y0 = sampleAt(channel, index - 1), y1 = sampleAt(channel, index);
    const float y2 = sampleAt(channel, index + 1), y3 = sampleAt(channel, index + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

float SamplerVoice::nextEnvelope() noexcept
{
    switch (m_stage) {
    case Stage::Attack:
        m_level += m_attackStep;
        if (m_level >= 1.0f) { m_level = 1.0f; m_stage = Stage::Decay; }
        break;
    case Stage::Decay:
        m_level = m_sustain + (m_level - m_sustain) * m_decayCoef;
        if (m_level - m_sustain <= silence) { m_level = m_sustain; m_stage = Stage::Sustain; }
        break;
    case Stage::Sustain:
        break;
    case Stage::Release:
        m_level *= m_releaseCoef;
        if (m_level <= silence) stop();
        break;
    case Stage::Off:
        break;
    }
    return m_level;
}

void SamplerVoice::render(float* left, float* right, int frames) noexcept
{
    if (m_stage == Stage::Off || !m_zone) return;
    const bool stereo = m_zone->audio->channels.size() >= 2;
    const std::int64_t length = m_zone->audio->frames;

    for (int i = 0; i < frames; ++i) {
        if (!m_looping && m_position >= double(length)) { stop(); return; }
        const float level = nextEnvelope();                // so a zero attack plays frame 0 in full
        if (m_stage == Stage::Off) return;
        if (m_stage == Stage::Sustain && m_sustain <= 0.0f) { stop(); return; }   // decayed to nothing

        const auto index = std::int64_t(m_position);
        const float t = float(m_position - double(index));
        const float l = interpolate(0, index, t);
        const float r = stereo ? interpolate(1, index, t) : l;
        left[i] += l * level * m_gainLeft;
        right[i] += r * level * m_gainRight;

        m_position += m_increment;
        if (m_looping && m_position >= double(m_loopEnd) + 1.0)
            m_position = double(m_loopStart)
                       + std::fmod(m_position - double(m_loopStart), double(m_loopEnd - m_loopStart + 1));
    }
}
