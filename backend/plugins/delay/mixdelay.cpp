#include "plugins/delay/mixdelay.h"

#include <algorithm>
#include <cmath>

namespace {
// Note lengths in quarter notes: 1/2, 1/4, 1/4 dotted, 1/8, 1/8 dotted, 1/8 triplet, 1/16.
constexpr std::array<double, 7> divisionBeats{2.0, 1.0, 1.5, 0.5, 0.75, 1.0 / 3.0, 0.25};
constexpr double maxDelayMs = 4000.0;
}

MixDelay::MixDelay() : MixEffect(mixfx::Delay) {}

double MixDelay::divisionMs(int division, double bpm) noexcept
{
    if (!std::isfinite(bpm) || bpm <= 0.0) bpm = 120.0;
    return std::min(maxDelayMs, divisionBeats[std::size_t(std::clamp(division, 0, 6))] * 60000.0 / bpm);
}

void MixDelay::prepareEffect(double rate, int)
{
    for (auto& line : m_lines) line.prepare(int(maxDelayMs * 0.001 * rate) + 8);
    m_time.prepare(rate, 60.0f);   // a moving time glides (a tape-like pitch bend), no clicks
    m_feedback.prepare(rate, 20.0f);
    m_mix.prepare(rate, 20.0f);
    m_cuts = {-1, -1};
}

void MixDelay::resetEffect() noexcept
{
    for (auto& line : m_lines) line.clear();
    for (auto* set : {&m_low, &m_high}) for (auto& f : *set) f.z = 0.0f;
    m_timeStarted = false;
    m_feedback.reset(control(Feedback) * 0.01f);
    m_mix.reset(control(Mix) * 0.01f);
    m_cuts = {-1, -1};
}

void MixDelay::processEffect(ProcessContext& context) noexcept
{
    const double rate = sampleRate();
    const int frames = context.audio.frames;
    const int channels = std::min(context.audio.channelCount, 2);
    if (control(LowCut) != m_cuts[0] || control(HighCut) != m_cuts[1]) {
        m_cuts = {control(LowCut), control(HighCut)};
        for (auto& f : m_low) f.set(m_cuts[0], rate);
        for (auto& f : m_high) f.set(m_cuts[1], rate);
    }
    const bool synced = control(Sync) >= 0.5f;
    const double ms = synced ? divisionMs(int(control(Division)), context.bpm) : double(control(Time));
    const float targetSamples = float(std::clamp(ms, 1.0, maxDelayMs) * 0.001 * rate);
    if (!m_timeStarted) { m_time.reset(targetSamples); m_timeStarted = true; }   // no glide at the start
    const bool pingPong = control(PingPong) >= 0.5f;

    for (int i = 0; i < frames; ++i) {
        const float delay = m_time.next(targetSamples);
        const float feedback = m_feedback.next(control(Feedback) * 0.01f);
        const float mix = m_mix.next(control(Mix) * 0.01f);
        const float inL = context.audio.channel(0)[i];
        const float inR = context.audio.channel(std::min(1, channels - 1))[i];
        std::array<float, 2> echo{m_lines[0].readFractional(delay), m_lines[1].readFractional(delay)};
        // Each repeat goes through the cuts; ping-pong feeds each side's repeats to the other,
        // and the input enters on the left only, so the first echo is left, then right...
        std::array<float, 2> back;
        for (std::size_t c = 0; c < 2; ++c) back[c] = m_high[c].low(m_low[c].high(echo[c])) * feedback;
        if (pingPong) {
            m_lines[0].push(0.5f * (inL + inR) + back[1]);
            m_lines[1].push(back[0]);
        } else {
            m_lines[0].push(inL + back[0]);
            m_lines[1].push(inR + back[1]);
        }
        const float dryGain = std::cos(mix * 1.5707963f), wetGain = std::sin(mix * 1.5707963f);
        context.audio.channel(0)[i] = dryGain * inL + wetGain * echo[0];
        if (channels > 1) context.audio.channel(1)[i] = dryGain * inR + wetGain * echo[1];
    }
}
