#include "instruments/harmonium/reedvoice.h"

#include <algorithm>
#include <cmath>

namespace harmonium {

namespace {

constexpr float silent = 1e-4f;                      // -80 dB: a released key is done
constexpr double releaseSeconds = 0.03;              // the valve shuts, the reed rings briefly
constexpr double fadeSeconds = 0.004;
constexpr double stopSeconds = 0.015;                // a stop opening or closing

// Per-frame factor so that a gap to the target shrinks to 1/e in `seconds`.
float approach(double seconds, double rate) noexcept
{
    const double frames = seconds * rate;
    return frames >= 1.0 ? float(std::exp(-1.0 / frames)) : 0.0f;
}

// Hand-made reeds are never tuned exactly: a fixed scatter of about +-1.5 cents per key. All
// reed sets of a key share it, so with no beating set the sets do not beat.
double scatterCents(int key, int rank) noexcept
{
    std::uint32_t h = std::uint32_t(key * 131 + rank * 7919 + 17);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (double(h % 1000u) / 999.0 - 0.5) * 3.0;
}

} // namespace

void ReedVoice::tune(const std::array<Rank, MaxRanks>& ranks) noexcept
{
    for (int r = 0; r < MaxRanks; ++r) {
        const auto& rank = ranks[std::size_t(r)];
        const double semitones = double(m_soundingKey + 12 * rank.octave - 69)
                               + (rank.detuneCents + scatterCents(m_soundingKey, 0)) / 100.0;
        m_frequency[std::size_t(r)] = 440.0 * std::exp2(semitones / 12.0);
        m_gainTarget[std::size_t(r)] = std::max(0.0f, rank.gain);
    }
}

void ReedVoice::start(int inputKey, int soundingKey, const std::array<Rank, MaxRanks>& ranks,
                      double sampleRate, double attackSeconds) noexcept
{
    const bool sounding = active() && m_soundingKey == soundingKey;
    m_inputKey = inputKey;
    m_soundingKey = soundingKey;
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    tune(ranks);
    if (!sounding) {
        for (int r = 0; r < MaxRanks; ++r) {
            // Each reed starts at its own point in the period, as real ones do.
            const double phase = scatterCents(soundingKey, r + 11) / 3.0 + 0.5;
            m_osc[std::size_t(r)].start(ranks[std::size_t(r)].timbre, m_frequency[std::size_t(r)], m_sampleRate, phase);
            m_gain[std::size_t(r)] = m_gainTarget[std::size_t(r)];
        }
        m_level = 0.0f;
    }
    m_attackCoef = approach(attackSeconds, m_sampleRate);
    m_releaseCoef = approach(releaseSeconds, m_sampleRate);
    m_fadeCoef = approach(fadeSeconds, m_sampleRate);
    m_gainCoef = approach(stopSeconds, m_sampleRate);
    m_stage = Stage::Attack;
}

void ReedVoice::setRanks(const std::array<Rank, MaxRanks>& ranks) noexcept
{
    if (active()) tune(ranks);
}

void ReedVoice::release() noexcept
{
    if (m_stage == Stage::Attack || m_stage == Stage::Hold) m_stage = Stage::Release;
}

void ReedVoice::fadeOut() noexcept
{
    if (active()) m_stage = Stage::Fade;
}

void ReedVoice::stop() noexcept
{
    m_stage = Stage::Off;
    m_level = 0.0f;
    m_inputKey = m_soundingKey = -1;
}

int ReedVoice::soundingRanks() const noexcept
{
    if (!active()) return 0;
    return int(std::count_if(m_gainTarget.begin(), m_gainTarget.end(), [](float g) { return g > 0.0f; }));
}

void ReedVoice::render(float* mono, int frames, const Air& air) noexcept
{
    if (!active()) return;
    for (int r = 0; r < MaxRanks; ++r)
        m_osc[std::size_t(r)].setFrequency(m_frequency[std::size_t(r)] * air.pitchRatio, m_sampleRate);

    // Air pressure sets how hard the reeds swing (drive) and how bright they are. It moves
    // slowly, so both are worked out at the block's ends and ramped in between.
    const auto brightnessOf = [](float p) { return std::clamp(std::pow(std::max(p, 0.0f), 0.8f), 0.0f, 1.0f); };
    const auto driveOf = [](float p) { return std::pow(std::max(p, 0.0f), 1.2f); };
    const float n = float(std::max(frames, 1));
    float brightness = brightnessOf(air.pressureFrom), drive = driveOf(air.pressureFrom);
    const float brightnessStep = (brightnessOf(air.pressureTo) - brightness) / n;
    const float driveStep = (driveOf(air.pressureTo) - drive) / n;
    for (int i = 0; i < frames; ++i) {
        switch (m_stage) {
        case Stage::Attack:
            m_level = 1.0f - (1.0f - m_level) * m_attackCoef;
            if (m_level > 0.999f) { m_level = 1.0f; m_stage = Stage::Hold; }
            break;
        case Stage::Release: m_level *= m_releaseCoef; break;
        case Stage::Fade:    m_level *= m_fadeCoef; break;
        default: break;
        }
        if (releasing() && m_level < silent) { stop(); return; }

        float sum = 0.0f;
        for (int r = 0; r < MaxRanks; ++r) {
            auto& gain = m_gain[std::size_t(r)];
            const float target = m_gainTarget[std::size_t(r)];
            gain = target + (gain - target) * m_gainCoef;
            if (target == 0.0f && gain < 1e-3f) gain = 0.0f;     // a closed stop is silent
            if (gain > 0.0f) sum += gain * m_osc[std::size_t(r)].next(brightness);
        }
        mono[i] += sum * m_level * drive;
        brightness += brightnessStep;
        drive += driveStep;
    }
}

} // namespace harmonium
