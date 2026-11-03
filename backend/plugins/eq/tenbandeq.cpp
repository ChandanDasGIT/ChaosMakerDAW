#include "tenbandeq.h"
#include "core/valueformat.h"

#include <algorithm>
#include <cmath>

TenBandEq::TenBandEq()
{
    for (int band = 0; band < 10; ++band) {
        const auto id = "band" + std::to_string(band + 1);
        m_gains[static_cast<std::size_t>(band)] = &addParameter(id, id, -12.0f, 12.0f, 0.0f, "dB");
        m_appliedGain[static_cast<std::size_t>(band)] = 0.0f;
    }
}

const std::array<BandDescriptor, 10>& TenBandEq::bandDescriptors()
{
    static const std::array<BandDescriptor, 10> descriptors = {{
        {0, 31.25f,  "31",  "31.25 Hz", -12.0f, 12.0f, 0.0f, 0.5f},
        {1, 62.5f,   "62",  "62.5 Hz",  -12.0f, 12.0f, 0.0f, 0.5f},
        {2, 125.0f,  "125", "125 Hz",   -12.0f, 12.0f, 0.0f, 0.5f},
        {3, 250.0f,  "250", "250 Hz",   -12.0f, 12.0f, 0.0f, 0.5f},
        {4, 500.0f,  "500", "500 Hz",   -12.0f, 12.0f, 0.0f, 0.5f},
        {5, 1000.0f, "1k",  "1 kHz",    -12.0f, 12.0f, 0.0f, 0.5f},
        {6, 2000.0f, "2k",  "2 kHz",    -12.0f, 12.0f, 0.0f, 0.5f},
        {7, 4000.0f, "4k",  "4 kHz",    -12.0f, 12.0f, 0.0f, 0.5f},
        {8, 8000.0f, "8k",  "8 kHz",    -12.0f, 12.0f, 0.0f, 0.5f},
        {9, 16000.0f,"16k", "16 kHz",   -12.0f, 12.0f, 0.0f, 0.5f}
    }};
    return descriptors;
}

const BandDescriptor& TenBandEq::bandDescriptor(int index)
{
    const auto& descriptors = bandDescriptors();
    if (index < 0 || static_cast<std::size_t>(index) >= descriptors.size()) {
        static const BandDescriptor fallback{};
        return fallback;
    }
    return descriptors[static_cast<std::size_t>(index)];
}

std::string TenBandEq::formatGain(float gainDb)
{
    return valueformat::withUnit(gainDb, 1, "dB");
}

bool TenBandEq::isValidGain(float gainDb) noexcept
{
    return std::isfinite(gainDb) && gainDb >= MinGainDb && gainDb <= MaxGainDb;
}

float TenBandEq::clampGain(float gainDb) noexcept
{
    if (!std::isfinite(gainDb)) return DefaultGainDb;
    return std::clamp(gainDb, MinGainDb, MaxGainDb);
}

void TenBandEq::prepare(double sampleRate, int)
{
    if (std::isfinite(sampleRate) && sampleRate > 1000.0)
        m_sampleRate = sampleRate;
    m_appliedGain.fill(-1000.0f);
    reset();
}

void TenBandEq::reset() noexcept
{
    for (auto& band : m_state)
        for (auto& channel : band)
            channel = {};
}

void TenBandEq::setBandGain(int band, float gainDb) noexcept
{
    if (band >= 0 && band < static_cast<int>(m_gains.size()))
        m_gains[static_cast<std::size_t>(band)]->set(clampGain(gainDb));
}

float TenBandEq::bandGain(int band) const noexcept
{
    if (band >= 0 && band < static_cast<int>(m_gains.size()) && m_gains[static_cast<std::size_t>(band)])
        return m_gains[static_cast<std::size_t>(band)]->get();
    return 0.0f;
}

void TenBandEq::updateBand(int band, float gainDb) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    constexpr float q = 1.4f;
    const float frequency = std::min(Frequencies[static_cast<std::size_t>(band)],
                                     static_cast<float>(m_sampleRate * 0.45));
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * pi * frequency / static_cast<float>(m_sampleRate);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float cosw = std::cos(w0);
    const float a0 = 1.0f + alpha / a;
    auto& c = m_coefficients[static_cast<std::size_t>(band)];
    c.b0 = (1.0f + alpha * a) / a0;
    c.b1 = (-2.0f * cosw) / a0;
    c.b2 = (1.0f - alpha * a) / a0;
    c.a1 = (-2.0f * cosw) / a0;
    c.a2 = (1.0f - alpha / a) / a0;
    m_appliedGain[static_cast<std::size_t>(band)] = gainDb;
}

void TenBandEq::process(ProcessContext& context) noexcept
{
    const int channelCount = std::min(context.audio.channelCount, 2);
    for (int band = 0; band < 10; ++band) {
        const auto bi = static_cast<std::size_t>(band);
        const float gain = m_gains[bi]->get();
        if (gain != m_appliedGain[bi])
            updateBand(band, gain);
        if (std::abs(gain) < 0.001f)
            continue;
        const auto c = m_coefficients[bi];
        for (int channel = 0; channel < channelCount; ++channel) {
            auto& state = m_state[bi][static_cast<std::size_t>(channel)];
            float* samples = context.audio.channel(channel);
            for (int frame = 0; frame < context.audio.frames; ++frame) {
                const float in = samples[frame];
                const float out = c.b0 * in + state.z1;
                state.z1 = c.b1 * in - c.a1 * out + state.z2;
                state.z2 = c.b2 * in - c.a2 * out;
                samples[frame] = out;
            }
        }
    }
}
