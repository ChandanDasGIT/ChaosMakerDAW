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

float SamplerVoice::Filter::process(int c, float x) noexcept
{
    const float v3 = x - ic2[c];
    const float v1 = a1 * ic1[c] + a2 * v3;
    const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
    ic1[c] = 2.0f * v1 - ic1[c];
    ic2[c] = 2.0f * v2 - ic2[c];
    return v2;
}

void SamplerVoice::start(const SampleZone& zone, int key, int velocity, double outputRate) noexcept
{
    m_zone = &zone;
    m_key = key;
    if (!(outputRate > 0.0)) outputRate = 48000.0;
    m_rate = outputRate;

    const auto& audio = *zone.audio;
    m_position = double(std::clamp<std::int64_t>(zone.offset, 0, audio.frames));
    m_delayFrames = std::isfinite(zone.delaySeconds) && zone.delaySeconds > 0.0f
                  ? std::int64_t(double(zone.delaySeconds) * outputRate) : 0;
    const double sourceRate = audio.sampleRate > 0.0 ? audio.sampleRate : outputRate;
    const double semitones = double(key - zone.rootKey) + double(zone.tuneCents) / 100.0;
    m_increment = std::exp2(semitones / 12.0) * sourceRate / outputRate;

    m_looping = zone.looping && zone.loopStart >= 0 && zone.loopEnd > zone.loopStart
             && zone.loopEnd < audio.frames;
    m_loopStart = zone.loopStart;
    m_loopEnd = zone.loopEnd;

    velocity = std::clamp(velocity, 0, 127);
    const float vel = float(velocity) / 127.0f;
    const float velocityGain = zone.velocityCurve ? (*zone.velocityCurve)[std::size_t(velocity)] : vel * vel;
    const float gain = velocityGain * std::pow(10.0f, zone.volumeDb / 20.0f);
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
    m_pedalHeld = false;
    m_sustain = std::clamp(zone.sustain, 0.0f, 1.0f);
    m_holdFrames = std::isfinite(zone.hold) && zone.hold > 0.0f ? std::int64_t(double(zone.hold) * outputRate) : 0;

    // The low-pass: below Nyquist, or no filter at all.
    m_filtered = std::isfinite(zone.cutoffHz) && zone.cutoffHz > 0.0f && zone.cutoffHz < 0.49 * outputRate;
    m_filter = Filter{};
    if (m_filtered) {
        const double q = std::numbers::sqrt2 / 2.0 * std::pow(10.0, std::clamp(double(zone.resonanceDb), -24.0, 40.0) / 20.0);
        m_filter.g = float(std::tan(std::numbers::pi * double(zone.cutoffHz) / outputRate));
        m_filter.k = float(1.0 / q);
        m_filter.a1 = 1.0f / (1.0f + m_filter.g * (m_filter.g + m_filter.k));
        m_filter.a2 = m_filter.g * m_filter.a1;
        m_filter.a3 = m_filter.g * m_filter.a2;
    }

    m_level = 0.0f;
    m_stage = Stage::Attack;
}

void SamplerVoice::release() noexcept
{
    if (m_stage != Stage::Off) m_stage = Stage::Release;
}

void SamplerVoice::release(float seconds) noexcept
{
    if (m_stage == Stage::Off) return;
    m_releaseCoef = fallCoefficient(seconds, m_rate);
    m_stage = Stage::Release;
}

void SamplerVoice::setReleaseSeconds(float seconds) noexcept
{
    if (m_stage != Stage::Release || m_fading) return;
    m_releaseCoef = fallCoefficient(seconds, m_rate);
}

void SamplerVoice::fadeOut() noexcept
{
    if (m_stage == Stage::Off) return;
    m_stage = Stage::Release;
    m_fading = true;
    m_pedalHeld = false;
    m_releaseCoef = std::min(m_releaseCoef, m_fadeCoef);   // never slower than the zone's own release
}

void SamplerVoice::fadeOut(float seconds) noexcept
{
    if (m_stage == Stage::Off) return;
    const bool wasReleasing = m_stage == Stage::Release;
    const float previous = m_releaseCoef;
    m_stage = Stage::Release;
    m_fading = true;
    m_pedalHeld = false;
    const float coef = fallCoefficient(std::max(seconds, fadeSeconds), m_rate);
    // A release already faster than this fade stays so (as fadeOut() does).
    m_releaseCoef = wasReleasing ? std::min(previous, coef) : coef;
}

void SamplerVoice::stop() noexcept
{
    m_stage = Stage::Off;
    m_level = 0.0f;
    m_zone = nullptr;
    m_key = -1;
    m_fading = false;
    m_pedalHeld = false;
    m_delayFrames = 0;
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
        if (m_level >= 1.0f) { m_level = 1.0f; m_stage = m_holdFrames > 0 ? Stage::Hold : Stage::Decay; }
        break;
    case Stage::Hold:
        if (--m_holdFrames <= 0) m_stage = Stage::Decay;
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

    int i = 0;
    if (m_delayFrames > 0) {                               // waiting for the sample to start
        const int wait = int(std::min<std::int64_t>(m_delayFrames, frames));
        m_delayFrames -= wait;
        i = wait;
        if (m_stage == Stage::Release && m_fading) { stop(); return; }   // choked before it began
    }
    for (; i < frames; ++i) {
        if (!m_looping && m_position >= double(length)) { stop(); return; }
        const float level = nextEnvelope();                // so a zero attack plays frame 0 in full
        if (m_stage == Stage::Off) return;
        if (m_stage == Stage::Sustain && m_sustain <= 0.0f) { stop(); return; }   // decayed to nothing

        const auto index = std::int64_t(m_position);
        const float t = float(m_position - double(index));
        float l = interpolate(0, index, t);
        float r = stereo ? interpolate(1, index, t) : l;
        if (m_filtered) {
            l = m_filter.process(0, l);
            r = stereo ? m_filter.process(1, r) : l;
        }
        left[i] += l * level * m_gainLeft;
        right[i] += r * level * m_gainRight;

        m_position += m_increment;
        if (m_looping && m_position >= double(m_loopEnd) + 1.0)
            m_position = double(m_loopStart)
                       + std::fmod(m_position - double(m_loopStart), double(m_loopEnd - m_loopStart + 1));
    }
}
