#include "plugins/chaosverb/chaosverb.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float twoPi = 6.28318530718f;
constexpr std::array<float, 8> spread{0.0f, 0.131f, 0.263f, 0.397f, 0.529f, 0.659f, 0.811f, 1.0f};
// Each line's LFO runs at the rate times this, so the lines never move together.
constexpr std::array<float, 8> rateSpread{1.0f, 1.13f, 0.87f, 1.29f, 0.79f, 1.07f, 0.93f, 1.21f};
constexpr std::array<float, 8> lineDiffuserMs{2.93f, 3.71f, 4.37f, 5.11f, 3.17f, 4.79f, 2.53f, 5.63f};
constexpr std::array<float, 4> inputDiffuserMs{4.71f, 3.61f, 12.61f, 9.29f};
constexpr std::array<float, 8> earlyTime{0.07f, 0.16f, 0.26f, 0.37f, 0.49f, 0.62f, 0.78f, 0.95f};
constexpr std::array<float, 8> earlyGain{0.9f, 0.75f, 0.66f, 0.55f, 0.45f, 0.36f, 0.27f, 0.2f};

struct ModeShape {
    float shortestMs, longestMs;   // the lines at size 60 %
    float earlySpreadMs;           // 0: no early reflections
    float modMs;                   // the lines' movement at full depth
    float diffusion;               // the late diffusion at Diffusion 100 %
    float decayScale;              // Ambience keeps its tail short
    float brightness;              // the loop's high cut times this
};
constexpr std::array<ModeShape, 7> modes{{
    {32, 104, 85, 1.2f, 0.62f, 1.0f, 1.0f},     // Concert Hall
    {28, 96, 70, 1.0f, 0.60f, 1.0f, 1.6f},      // Bright Hall
    {9, 37, 0, 0.5f, 0.72f, 1.0f, 1.3f},        // Plate
    {7, 31, 26, 0.35f, 0.55f, 1.0f, 1.0f},      // Room
    {13, 47, 42, 0.6f, 0.65f, 1.0f, 1.1f},      // Chamber
    {22, 84, 55, 3.2f, 0.6f, 1.0f, 1.2f},       // Chorus Space
    {6, 21, 32, 0.4f, 0.5f, 0.45f, 1.0f},       // Ambience
}};

struct ColourShape {
    float inputHz;     // the band the input is limited to
    float loopHz;      // the highs each trip keeps
    float movement;    // modulation depth times this
};
constexpr std::array<ColourShape, 3> colours{{
    {7000, 6500, 0.4f},      // 1970s: dark, steady
    {11000, 10500, 1.5f},    // 1980s: bright, a lot of movement
    {19000, 16000, 1.0f},    // Now: full range, clean
}};

constexpr float maxScale = 1.6f;
constexpr float maxLineMs = 104.0f * maxScale + 3.2f * 1.5f + 2.0f;
constexpr float maxPreMs = 300.0f + 85.0f * maxScale + 2.0f;

} // namespace

ChaosVerb::ChaosVerb() : MixEffect(mixfx::ChaosVerb) {}

void ChaosVerb::prepareEffect(double rate, int)
{
    for (auto& line : m_lines) line.prepare(int(maxLineMs * 0.001 * rate) + 8);
    for (std::size_t i = 0; i < 8; ++i) {
        m_lineDiffusers[i].prepare(int(lineDiffuserMs[i] * 0.001 * rate) + 8);
        m_lineDiffuserLength[i] = float(lineDiffuserMs[i] * 0.001 * rate);
        m_bassSplit[i].set(250.0f, rate);
        m_trebleSplit[i].set(3500.0f, rate);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        m_inputDiffusers[i].prepare(int(inputDiffuserMs[i] * 0.001 * rate) + 8);
        m_inputDiffuserLength[i] = float(inputDiffuserMs[i] * 0.001 * rate);
    }
    m_preDelay.prepare(int(maxPreMs * 0.001 * rate) + 8);
    for (auto* s : {&m_mix, &m_width, &m_earlyLate}) s->prepare(rate, 20.0f);
    m_preDelaySmoother.prepare(rate, 50.0f);
    m_depth.prepare(rate, 50.0f);
    m_shapeKey.fill(-1.0f);
    m_tone = {-1, -1};
}

void ChaosVerb::resetEffect() noexcept
{
    for (auto& line : m_lines) line.clear();
    for (auto& d : m_lineDiffusers) d.clear();
    for (auto& d : m_inputDiffusers) d.clear();
    m_preDelay.clear();
    for (auto* set : {&m_bassSplit, &m_trebleSplit, &m_loopColour})
        for (auto& f : *set) f.z = 0.0f;
    m_bandwidth.z = 0.0f;
    for (std::size_t i = 0; i < 8; ++i) m_phase[i] = float(i) / 8.0f;   // spread so the lines start apart
    for (auto& channel : m_lowState) channel = {};
    for (auto& channel : m_highState) channel = {};
    m_mix.reset(control(Mix) * 0.01f);
    m_width.reset(control(Width) * 0.01f);
    m_earlyLate.reset(control(EarlyLate) * 0.01f);
    m_preDelaySmoother.reset(control(PreDelay));
    m_shapeKey.fill(-1.0f);
    m_tone = {-1, -1};
    updateShape();
    m_depth.reset(m_modDepthSamples * control(ModDepth) * 0.01f);
    updateTone();
}

void ChaosVerb::updateShape() noexcept
{
    const std::array<float, 8> key{control(Mode), control(Colour), control(Decay), control(Size),
                                   control(Diffusion), control(BassDecay), control(TrebleDecay), 0.0f};
    if (key == m_shapeKey) return;
    m_shapeKey = key;
    const double rate = sampleRate();
    const auto& mode = modes[std::size_t(std::clamp(int(key[0]), 0, 6))];
    const auto& colour = colours[std::size_t(std::clamp(int(key[1]), 0, 2))];
    const float scale = 0.4f + key[3] * 0.012f;   // size 0..100 % -> 0.4..1.6
    const float decay = key[2] * mode.decayScale;
    for (std::size_t i = 0; i < 8; ++i) {
        const float ms = (mode.shortestMs + (mode.longestMs - mode.shortestMs) * spread[i]) * scale;
        // The loop length includes the line's own diffuser.
        m_length[i] = std::max(16.0f, float(ms * 0.001 * rate));
        const float loop = m_length[i] + m_lineDiffuserLength[i];
        m_gainMid[i] = reverb::decayGain(loop, decay, rate);
        m_gainLow[i] = reverb::decayGain(loop, decay * key[5], rate);
        m_gainHigh[i] = reverb::decayGain(loop, decay * key[6], rate);
        m_loopColour[i].set(std::min(colour.loopHz * mode.brightness, float(rate * 0.45)), rate);
    }
    m_bandwidth.set(colour.inputHz, rate);
    m_earlySpread = float(mode.earlySpreadMs * scale * 0.001 * rate);
    m_earlyLevel = mode.earlySpreadMs > 0.0f ? 1.0f : 0.0f;
    m_modDepthSamples = float(mode.modMs * colour.movement * 0.001 * rate);
    m_lateDiffusion = mode.diffusion * key[4] * 0.01f;
}

void ChaosVerb::updateTone() noexcept
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

void ChaosVerb::processEffect(ProcessContext& context) noexcept
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
    const float rateStep = control(ModRate) / float(rate);
    const float inputDiffusion = 0.75f * control(Diffusion) * 0.01f;
    constexpr float lateLevel = 0.6f * 0.35355339f;
    constexpr float earlyLevel = 0.5f;

    for (int i = 0; i < frames; ++i) {
        const float mix = m_mix.next(control(Mix) * 0.01f);
        const float width = m_width.next(control(Width) * 0.01f);
        const float balance = m_earlyLate.next(control(EarlyLate) * 0.01f);
        const float depth = m_depth.next(m_modDepthSamples * control(ModDepth) * 0.01f);
        const float preDelay = std::max(1.0f, float(m_preDelaySmoother.next(control(PreDelay)) * 0.001 * rate));
        const float left = context.audio.channel(0)[i];
        const float right = context.audio.channel(std::min(1, channels - 1))[i];

        m_preDelay.push(m_bandwidth.low(0.5f * (left + right)));
        float input = m_preDelay.readFractional(preDelay);
        for (std::size_t d = 0; d < 4; ++d) input = m_inputDiffusers[d].process(input, m_inputDiffuserLength[d], inputDiffusion);

        std::array<float, 8> state;
        for (std::size_t l = 0; l < 8; ++l) {
            m_phase[l] += rateStep * rateSpread[l];
            if (m_phase[l] >= 1.0f) m_phase[l] -= 1.0f;
            const float moved = m_length[l] + depth * (1.0f + std::sin(twoPi * m_phase[l]));
            state[l] = m_lineDiffusers[l].process(m_lines[l].readFractional(moved), m_lineDiffuserLength[l], m_lateDiffusion);
        }
        float lateL = 0.0f, lateR = 0.0f;
        for (std::size_t l = 0; l < 8; ++l) {
            lateL += reverb::leftTaps[l] * state[l];
            lateR += reverb::rightTaps[l] * state[l];
        }
        reverb::hadamard8(state);
        for (std::size_t l = 0; l < 8; ++l) {
            // Three bands, each losing what its decay asks for, then the era's high cut.
            const float x = state[l];
            const float low = m_bassSplit[l].low(x);
            const float high = m_trebleSplit[l].high(x);
            const float mid = x - low - high;
            const float y = low * m_gainLow[l] + mid * m_gainMid[l] + high * m_gainHigh[l];
            m_lines[l].push(m_loopColour[l].low(y) + input);
        }

        float earlyL = 0.0f, earlyR = 0.0f;
        if (m_earlyLevel > 0.0f) {
            for (std::size_t e = 0; e < earlyTime.size(); ++e) {
                const float tap = m_preDelay.readFractional(preDelay + earlyTime[e] * m_earlySpread) * earlyGain[e];
                (e % 2 ? earlyR : earlyL) += tap;
                (e % 2 ? earlyL : earlyR) += 0.35f * tap;
            }
        }
        // Early / late, equal power: 0 % only reflections, 100 % only the tail.
        const float earlyShare = std::cos(balance * 1.5707963f) * m_earlyLevel * earlyLevel;
        const float lateShare = (m_earlyLevel > 0.0f ? std::sin(balance * 1.5707963f) : 1.0f) * lateLevel;
        float wetL = earlyL * earlyShare + lateL * lateShare;
        float wetR = earlyR * earlyShare + lateR * lateShare;
        wetL = filter(m_highCut, m_highState[0], filter(m_lowCut, m_lowState[0], wetL));
        wetR = filter(m_highCut, m_highState[1], filter(m_lowCut, m_lowState[1], wetR));
        const float midWet = 0.5f * (wetL + wetR), sideWet = 0.5f * (wetL - wetR) * width;
        const float dryGain = std::cos(mix * 1.5707963f), wetGain = std::sin(mix * 1.5707963f);
        context.audio.channel(0)[i] = dryGain * left + wetGain * (midWet + sideWet);
        if (channels > 1) context.audio.channel(1)[i] = dryGain * right + wetGain * (midWet - sideWet);
    }
}
