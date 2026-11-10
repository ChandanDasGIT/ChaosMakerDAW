#include "plugins/reverb/roomreverb.h"

#include <algorithm>
#include <cmath>

namespace {

// Where each line's length sits between the type's shortest and longest (irregular, so the
// echoes do not line up into a pitch).
constexpr std::array<float, 8> spread{0.0f, 0.137f, 0.271f, 0.389f, 0.523f, 0.661f, 0.809f, 1.0f};
// The input allpasses' lengths in ms.
constexpr std::array<float, 6> diffuserMs{4.77f, 3.59f, 12.73f, 9.31f, 7.13f, 2.27f};
// The early reflections: when (as a share of the spread) and how loud, left / right sign.
constexpr std::array<float, 6> earlyTime{0.11f, 0.23f, 0.37f, 0.52f, 0.71f, 0.93f};
constexpr std::array<float, 6> earlyLevel{0.85f, 0.7f, 0.6f, 0.45f, 0.35f, 0.25f};

struct TypeShape {
    float shortestMs, longestMs;   // delay lines at size 50 %
    int diffusers;
    float diffusion;
    float early;                   // early reflections' level
    float earlySpreadMs;
};
constexpr std::array<TypeShape, 3> shapes{{
    {9.0f, 37.0f, 4, 0.60f, 0.55f, 28.0f},    // Room: short, with clear first reflections
    {7.0f, 29.0f, 6, 0.75f, 0.0f, 0.0f},      // Plate: dense from the start, no reflections
    {27.0f, 93.0f, 4, 0.65f, 0.35f, 70.0f},   // Hall: long lines, slower build, wide reflections
}};

constexpr float maxSizeScale = 1.5f;
constexpr float maxLineMs = 93.0f * maxSizeScale;
constexpr float maxPreDelayMs = 200.0f + 70.0f * maxSizeScale;

} // namespace

RoomReverb::RoomReverb() : MixEffect(mixfx::Reverb) {}

void RoomReverb::prepareEffect(double rate, int)
{
    for (auto& line : m_lines) line.prepare(int(maxLineMs * 0.001 * rate) + 8);
    for (std::size_t i = 0; i < m_diffusers.size(); ++i) m_diffusers[i].prepare(int(diffuserMs[i] * 0.001 * rate) + 8);
    m_preDelay.prepare(int(maxPreDelayMs * 0.001 * rate) + 8);
    m_mix.prepare(rate, 20.0f);
    m_width.prepare(rate, 20.0f);
    m_preDelaySmoother.prepare(rate, 50.0f);
    for (std::size_t i = 0; i < m_diffusers.size(); ++i) m_diffuserLength[i] = float(diffuserMs[i] * 0.001 * rate);
    m_shapeKey = {-1, -1, -1, -1};
    m_tone = {-1, -1};
}

void RoomReverb::resetEffect() noexcept
{
    for (auto& line : m_lines) line.clear();
    for (auto& d : m_diffusers) d.clear();
    m_preDelay.clear();
    for (auto& f : m_damping) f.z = 0.0f;
    for (auto& channel : m_lowState) channel = {};
    for (auto& channel : m_highState) channel = {};
    m_mix.reset(control(Mix) * 0.01f);
    m_width.reset(control(Width) * 0.01f);
    m_preDelaySmoother.reset(control(PreDelay));
    m_shapeKey = {-1, -1, -1, -1};
    m_tone = {-1, -1};
    updateShape();
    updateTone();
}

void RoomReverb::updateShape() noexcept
{
    const std::array<float, 4> key{control(Type), control(Size), control(Decay), control(Damping)};
    if (key == m_shapeKey) return;
    m_shapeKey = key;
    const double rate = sampleRate();
    const auto& shape = shapes[std::size_t(std::clamp(int(key[0]), 0, 2))];
    const float scale = 0.5f + key[1] * 0.01f;   // size 0..100 % -> 0.5..1.5
    for (std::size_t i = 0; i < 8; ++i) {
        const float ms = (shape.shortestMs + (shape.longestMs - shape.shortestMs) * spread[i]) * scale;
        m_length[i] = std::max(8.0f, float(ms * 0.001 * rate));
        m_gain[i] = reverb::decayGain(m_length[i], key[2], rate);
    }
    // Damping 0..100 %: the loop's highs roll off from 18 kHz down to 1.5 kHz.
    const float cutoff = 18000.0f * std::pow(1500.0f / 18000.0f, key[3] * 0.01f);
    for (auto& f : m_damping) f.set(cutoff, rate);
    m_diffuserCount = shape.diffusers;
    m_diffusion = shape.diffusion;
    m_earlyGain = shape.early;
    m_earlySpread = float(shape.earlySpreadMs * scale * 0.001 * rate);
}

void RoomReverb::updateTone() noexcept
{
    const std::array<float, 2> tone{control(LowCut), control(HighCut)};
    if (tone == m_tone) return;
    m_tone = tone;
    const double rate = sampleRate();
    eqband::Settings low;
    low.shape = eqband::Shape::LowCut;
    low.frequencyHz = tone[0];
    m_lowCut = eqband::design(eqband::sanitize(low, rate), rate);
    eqband::Settings high;
    high.shape = eqband::Shape::HighCut;
    high.frequencyHz = tone[1];
    m_highCut = eqband::design(eqband::sanitize(high, rate), rate);
}

void RoomReverb::processEffect(ProcessContext& context) noexcept
{
    updateShape();
    updateTone();
    const double rate = sampleRate();
    const int frames = context.audio.frames;
    const int channels = std::min(context.audio.channelCount, 2);
    const auto filter = [](const eqband::Design& d, std::array<biquad::State, eqband::MaxSections>& s, float x) {
        double y = x;
        for (int k = 0; k < d.sectionCount; ++k) y = biquad::process(d.sections[std::size_t(k)], s[std::size_t(k)], y);
        return float(y);
    };
    constexpr float wetLevel = 0.6f;

    for (int i = 0; i < frames; ++i) {
        const float mix = m_mix.next(control(Mix) * 0.01f);
        const float width = m_width.next(control(Width) * 0.01f);
        const float preDelay = std::max(1.0f, float(m_preDelaySmoother.next(control(PreDelay)) * 0.001 * rate));
        const float left = context.audio.channel(0)[i];
        const float right = context.audio.channel(std::min(1, channels - 1))[i];

        // Into the pre-delay; the network and the early reflections read from it.
        m_preDelay.push(0.5f * (left + right));
        float input = m_preDelay.readFractional(preDelay);
        for (int d = 0; d < m_diffuserCount; ++d)
            input = m_diffusers[std::size_t(d)].process(input, m_diffuserLength[std::size_t(d)], m_diffusion);

        std::array<float, 8> state;
        for (std::size_t l = 0; l < 8; ++l) state[l] = m_lines[l].readFractional(m_length[l]);
        float wetL = 0.0f, wetR = 0.0f;
        for (std::size_t l = 0; l < 8; ++l) {
            wetL += reverb::leftTaps[l] * state[l];
            wetR += reverb::rightTaps[l] * state[l];
        }
        reverb::hadamard8(state);
        for (std::size_t l = 0; l < 8; ++l)
            m_lines[l].push(m_damping[l].low(state[l] * m_gain[l]) + input);

        if (m_earlyGain > 0.0f) {
            for (std::size_t e = 0; e < earlyTime.size(); ++e) {
                const float tap = m_preDelay.readFractional(preDelay + earlyTime[e] * m_earlySpread) * earlyLevel[e] * m_earlyGain;
                (e % 2 ? wetR : wetL) += tap;
                (e % 2 ? wetL : wetR) += 0.4f * tap;
            }
        }
        wetL = filter(m_highCut, m_highState[0], filter(m_lowCut, m_lowState[0], wetL * wetLevel * 0.35355339f));
        wetR = filter(m_highCut, m_highState[1], filter(m_lowCut, m_lowState[1], wetR * wetLevel * 0.35355339f));
        // Width: 0 % mono, 100 % as the network makes it.
        const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * width;
        // Equal-power mix: the reverb does not correlate with the dry track, so this keeps the
        // loudness steady (25 %: the dry voice down 0.7 dB, the reverb at -8.3 dB).
        const float dryGain = std::cos(mix * 1.5707963f), wetGain = std::sin(mix * 1.5707963f);
        const float outL = dryGain * left + wetGain * (mid + side);
        const float outR = dryGain * right + wetGain * (mid - side);
        context.audio.channel(0)[i] = outL;
        if (channels > 1) context.audio.channel(1)[i] = outR;
    }
}
