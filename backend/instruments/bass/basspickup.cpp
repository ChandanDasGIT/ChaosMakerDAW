#include "instruments/bass/basspickup.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace bass {

void pickupTaps(double period, double fret, double blend, PickupTap& neck, PickupTap& bridge) noexcept
{
    const double shorter = std::exp2(std::max(fret, 0.0) / 12.0);   // fretting shortens the string
    blend = std::clamp(blend, 0.0, 1.0);
    const auto tap = [&](double at, double mix) {
        const double b = std::min(at * shorter, 0.5);
        // The comb's gain at the fundamental is 2 sin(pi b); even most of it out.
        const double level = 2.0 * std::sin(std::numbers::pi * b);
        return PickupTap{b * period, mix / std::pow(std::max(level, 0.05), 0.75)};
    };
    neck = tap(NeckPickupAt, 1.0 - blend);
    bridge = tap(BridgePickupAt, blend);
}

void ToneStage::prepare(double sampleRate) noexcept
{
    m_rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_attack = 1.0 - std::exp(-1.0 / (0.001 * m_rate));
    m_release = 1.0 - std::exp(-1.0 / (0.15 * m_rate));
    reset();
}

void ToneStage::reset() noexcept
{
    m_tone1 = m_tone2 = 0.0;
    m_envelope = 0.0;
}

void ToneStage::set(double tone, double drive, double compression) noexcept
{
    // Tone: a 12 dB / octave low-pass from 350 Hz (closed) to 16 kHz (open).
    const double hz = std::min(350.0 * std::pow(16000.0 / 350.0, std::clamp(tone, 0.0, 1.0)), 0.45 * m_rate);
    m_toneCoef = 1.0 - std::exp(-2.0 * std::numbers::pi * hz / m_rate);

    m_drive = std::clamp(drive, 0.0, 1.0);
    m_preGain = 1.0 + 24.0 * m_drive * m_drive;
    m_postGain = 1.0 / std::sqrt(m_preGain);

    m_compression = std::clamp(compression, 0.0, 1.0);
    const double thresholdDb = -8.0 - 22.0 * m_compression;
    const double ratio = 1.0 + 7.0 * m_compression;
    m_threshold = thresholdDb;
    m_slope = 1.0 - 1.0 / ratio;
    // A third of what the compressor takes off at full scale comes back.
    m_makeup = std::pow(10.0, -thresholdDb * m_slope * 0.35 / 20.0);
}

float ToneStage::process(float input) noexcept
{
    double x = input;
    m_tone1 += m_toneCoef * (x - m_tone1);
    m_tone2 += m_toneCoef * (m_tone1 - m_tone2);
    x = m_tone2;

    if (m_drive > 0.0) {
        const double driven = std::tanh(m_preGain * x) * m_postGain;
        const double mix = std::min(1.0, m_drive * 3.0);
        x += mix * (driven - x);
    }

    if (m_compression > 0.0) {
        const double level = std::abs(x);
        m_envelope += (level > m_envelope ? m_attack : m_release) * (level - m_envelope);
        const double db = 20.0 * std::log10(m_envelope + 1e-9);
        const double over = db - m_threshold;
        const double gainDb = over > 0.0 ? -over * m_slope : 0.0;
        x *= std::pow(10.0, gainDb / 20.0) * m_makeup;
    }
    return float(x);
}

} // namespace bass
