#include "plugins/denoise/denoise.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

std::array<float, Denoise::Size> sqrtHann()
{
    std::array<float, Denoise::Size> w{};
    for (int i = 0; i < Denoise::Size; ++i)
        w[std::size_t(i)] = float(std::sqrt(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / Denoise::Size)));
    return w;
}

// sqrt-Hann in and out is a Hann window per frame; four of them per frame length (hop
// Size / 4) add up to 2, so the frames are scaled by a half.
constexpr float OverlapScale = 0.5f;

} // namespace

Denoise::Denoise() : MixEffect(mixfx::Denoise), m_window(sqrtHann()) {}

std::vector<float> Denoise::learnProfile(std::span<const float> mono)
{
    if (mono.size() < std::size_t(Size)) return {};
    const Fft fft(Size);
    const auto window = sqrtHann();
    std::vector<std::complex<double>> frame(static_cast<std::size_t>(Size));
    std::vector<double> sum(std::size_t(Bins), 0.0);
    std::size_t frames = 0;
    for (std::size_t start = 0; start + std::size_t(Size) <= mono.size(); start += std::size_t(Hop), ++frames) {
        for (int i = 0; i < Size; ++i)
            frame[std::size_t(i)] = double(mono[start + std::size_t(i)]) * window[std::size_t(i)];
        fft.forward(frame.data());
        for (int k = 0; k < Bins; ++k) sum[std::size_t(k)] += std::abs(frame[std::size_t(k)]);
    }
    std::vector<float> profile(static_cast<std::size_t>(Bins));
    for (int k = 0; k < Bins; ++k) {
        const double level = sum[std::size_t(k)] / double(frames);
        profile[std::size_t(k)] = std::isfinite(level) ? float(level) : 0.0f;
    }
    return profile;
}

std::vector<double> Denoise::curveDb(std::span<const float> levels, std::span<const double> frequencies, double sampleRate)
{
    std::vector<double> out(frequencies.size(), -120.0);
    if (levels.size() != std::size_t(Bins) || !(sampleRate > 0)) return out;
    double windowSum = 0.0;
    for (const float w : sqrtHann()) windowSum += w;
    const double binHz = sampleRate / Size;
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        // The band between the neighbours' midpoints (geometric), at least the nearest bin.
        const double f = frequencies[i];
        const double low = i > 0 ? std::sqrt(f * frequencies[i - 1]) : f;
        const double high = i + 1 < frequencies.size() ? std::sqrt(f * frequencies[i + 1]) : f;
        int first = int(std::floor(low / binHz)), last = int(std::ceil(high / binHz));
        first = std::clamp(first, 0, Bins - 1);
        last = std::clamp(std::max(last, first), 0, Bins - 1);
        float loudest = 0.0f;
        for (int k = first; k <= last; ++k) loudest = std::max(loudest, levels[std::size_t(k)]);
        out[i] = std::max(-120.0, 20.0 * std::log10(std::max(1e-9, 2.0 * double(loudest) / windowSum)));
    }
    return out;
}

void Denoise::applyExtra(const mixfx::Settings& settings) noexcept
{
    auto& slot = m_profiles[std::size_t(m_writing)];
    const bool has = settings.profile.size() == std::size_t(Bins);
    for (int k = 0; k < Bins; ++k) slot[std::size_t(k)] = has ? settings.profile[std::size_t(k)] : 0.0f;
    slot[Bins] = has ? 1.0f : 0.0f;
    m_writing = m_shared.exchange(m_writing | Fresh, std::memory_order_acq_rel) & 3;
}

void Denoise::prepareEffect(double, int maxFrames)
{
    for (int c = 0; c < 2; ++c) {
        m_input[std::size_t(c)].assign(std::size_t(Size), 0.0f);
        m_output[std::size_t(c)].assign(std::size_t(Size), 0.0f);
        m_spectrum[std::size_t(c)].assign(std::size_t(Size), {});
    }
    m_mono.assign(std::size_t(std::max(maxFrames, 1)), 0.0f);
}

void Denoise::resetEffect() noexcept
{
    for (int c = 0; c < 2; ++c) {
        std::fill(m_input[std::size_t(c)].begin(), m_input[std::size_t(c)].end(), 0.0f);
        std::fill(m_output[std::size_t(c)].begin(), m_output[std::size_t(c)].end(), 0.0f);
    }
    m_gain.fill(1.0f);
    m_hopPos = 0;
}

std::size_t Denoise::readInput(std::span<float> out) noexcept
{
    const std::size_t count = std::min(out.size(), m_inRing.availableToRead());
    return count > 0 && m_inRing.pop(out.first(count)) ? count : 0;
}

std::size_t Denoise::readOutput(std::span<float> out) noexcept
{
    const std::size_t count = std::min(out.size(), m_outRing.availableToRead());
    return count > 0 && m_outRing.pop(out.first(count)) ? count : 0;
}

void Denoise::processEffect(ProcessContext& context) noexcept
{
    if (m_shared.load(std::memory_order_acquire) & Fresh)
        m_reading = m_shared.exchange(m_reading, std::memory_order_acq_rel) & 3;
    const int frames = context.audio.frames;
    const int channels = std::min(context.audio.channelCount, 2);
    const bool listening = m_listening.load(std::memory_order_relaxed) && std::size_t(frames) <= m_mono.size();
    if (listening) {
        for (int i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += context.audio.channel(c)[i];
            m_mono[std::size_t(i)] = sum / float(channels);
        }
        m_inRing.push(std::span<const float>(m_mono.data(), std::size_t(frames)));
    }
    const int start = Size - Hop;
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
            float& sample = context.audio.channel(c)[i];
            m_input[std::size_t(c)][std::size_t(start + m_hopPos)] = sample;
            sample = m_output[std::size_t(c)][std::size_t(m_hopPos)];
        }
        if (++m_hopPos == Hop) {
            processFrame(channels);
            m_hopPos = 0;
        }
    }
    if (listening) {
        for (int i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += context.audio.channel(c)[i];
            m_mono[std::size_t(i)] = sum / float(channels);
        }
        m_outRing.push(std::span<const float>(m_mono.data(), std::size_t(frames)));
    }
}

void Denoise::processFrame(int channels) noexcept
{
    for (int c = 0; c < channels; ++c) {
        auto& spectrum = m_spectrum[std::size_t(c)];
        const auto& input = m_input[std::size_t(c)];
        for (int i = 0; i < Size; ++i) spectrum[std::size_t(i)] = double(input[std::size_t(i)]) * m_window[std::size_t(i)];
        m_fft.forward(spectrum.data());
    }
    const auto& profile = m_profiles[std::size_t(m_reading)];
    const bool learned = profile[Bins] > 0.5f;
    const float floorGain = float(std::pow(10.0, -double(control(Reduction)) / 20.0));
    const float over = float(std::pow(10.0, double(control(Sensitivity)) / 20.0));
    const double smoothingSeconds = double(control(Smoothing)) / 1000.0;
    const float fall = smoothingSeconds > 0.0 ? float(std::exp(-double(Hop) / (smoothingSeconds * sampleRate()))) : 0.0f;
    const bool listen = control(Listen) >= 0.5f;

    // Each bin's gain: how much of it is above the noise, held a while after it falls.
    for (int k = 0; k < Bins; ++k) {
        float level = 0.0f;
        for (int c = 0; c < channels; ++c) level += float(std::abs(m_spectrum[std::size_t(c)][std::size_t(k)]));
        level /= float(channels);
        float gain = 1.0f;
        if (learned) gain = level > 0.0f ? std::max(floorGain, 1.0f - over * profile[std::size_t(k)] / level) : floorGain;
        float& held = m_gain[std::size_t(k)];
        held = gain >= held ? gain : gain + (held - gain) * fall;
        m_level[std::size_t(k)] = held;
    }
    // Averaged with the neighbours (5 bins), never under the floor.
    double removed = 0.0;
    for (int k = 0; k < Bins; ++k) {
        float sum = 0.0f;
        int count = 0;
        for (int j = std::max(0, k - 2); j <= std::min(Bins - 1, k + 2); ++j) { sum += m_level[std::size_t(j)]; ++count; }
        float gain = learned ? std::max(floorGain, sum / float(count)) : 1.0f;
        removed += double(gain);
        if (listen) gain = 1.0f - gain;
        for (int c = 0; c < channels; ++c) {
            auto& spectrum = m_spectrum[std::size_t(c)];
            spectrum[std::size_t(k)] *= gain;
            if (k > 0 && k < Size / 2) spectrum[std::size_t(Size - k)] *= gain;
        }
    }
    publishReductionDb(learned ? float(std::max(0.0, -20.0 * std::log10(std::max(removed / Bins, 1e-6)))) : 0.0f);

    for (int c = 0; c < channels; ++c) {
        auto& spectrum = m_spectrum[std::size_t(c)];
        m_fft.inverse(spectrum.data());
        auto& output = m_output[std::size_t(c)];
        auto& input = m_input[std::size_t(c)];
        // Move both on by a hop, then add this frame to the output.
        std::copy(output.begin() + Hop, output.end(), output.begin());
        std::fill(output.end() - Hop, output.end(), 0.0f);
        for (int i = 0; i < Size; ++i)
            output[std::size_t(i)] += float(spectrum[std::size_t(i)].real()) * m_window[std::size_t(i)] * OverlapScale;
        std::copy(input.begin() + Hop, input.end(), input.begin());
    }
}
