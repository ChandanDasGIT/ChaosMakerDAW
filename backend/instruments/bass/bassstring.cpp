#include "instruments/bass/bassstring.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace bass {

namespace {

constexpr double Stiffness = -0.18;                // the all-pass coefficient
constexpr double ReleasedRingSeconds = 0.09;       // a lifted finger stops the string this fast
constexpr double ReleasedOvertoneSeconds = 0.035;
constexpr double MutedRingSeconds = 0.12;          // full palm mute
constexpr double MutedOvertoneSeconds = 0.03;
constexpr double QuietLevel = 2e-5;                // below this for a while, the string is done

double keyHz(double key) noexcept { return 440.0 * std::exp2((key - 69.0) / 12.0); }

// Geometric blend: times are heard on a log scale.
double towards(double from, double to, double t) noexcept
{
    return std::exp(std::log(from) + (std::log(to) - std::log(from)) * std::clamp(t, 0.0, 1.0));
}

} // namespace

void BassString::prepare(double sampleRate)
{
    int size = 1;
    const int needed = int(std::ceil(sampleRate / LowestHz)) + 8;
    while (size < needed) size <<= 1;
    m_line.assign(std::size_t(size), 0.0f);
    m_mask = size - 1;
    m_write = 0;
    stop();
}

void BassString::start(int openKey, double key, NoteKind kind, double strength, double brightness,
                       double strikeAt, const StringControls& c) noexcept
{
    if (m_line.empty()) return;
    std::fill(m_line.begin(), m_line.end(), 0.0f);
    m_write = 0;
    m_lossState = m_allpassIn = m_allpassOut = 0.0;
    m_openKey = openKey;
    m_kind = kind;
    m_pitch = m_target = key;
    m_slideStep = 0.0;
    m_active = true;
    m_released = false;
    m_fade = 1.0;
    m_fadeStep = 0.0;
    m_peak = 0.0;
    m_quietFrames = 0;
    design(c);
    m_exciter.start(kind, strength, brightness, strikeAt, m_period, c.sampleRate);
}

void BassString::slideTo(double key, double seconds, double sampleRate) noexcept
{
    m_target = key;
    const double samples = std::max(1.0, seconds * sampleRate);
    m_slideStep = std::abs(m_target - m_pitch) / samples;
    m_released = false;
}

void BassString::release() noexcept { m_released = true; }

void BassString::fadeOut(double sampleRate) noexcept
{
    if (!m_active) return;
    m_fadeStep = 1.0 / std::max(1.0, 0.006 * sampleRate);
}

void BassString::stop() noexcept
{
    m_active = false;
    m_released = false;
    m_fadeStep = 0.0;
    m_fade = 1.0;
    m_peak = 0.0;
    m_exciter.stop();
}

void BassString::design(const StringControls& c) noexcept
{
    const double rate = c.sampleRate;
    const double hz = std::clamp(keyHz(m_pitch + c.bendSemitones), LowestHz, 0.2 * rate);
    m_period = rate / hz;

    // The two decay times for this note.
    double ring = c.ringSeconds * std::clamp(std::pow(82.4 / hz, 0.3), 0.6, 1.4);   // low strings ring longer
    double overtones = c.overtoneSeconds;
    if (m_kind == NoteKind::Slap || m_kind == NoteKind::Pop) overtones *= 1.8;      // the string rattles the frets
    if (m_kind == NoteKind::Muted) {
        ring = towards(ring, MutedRingSeconds, c.muteAmount);
        overtones = towards(overtones, MutedOvertoneSeconds, c.muteAmount);
    }
    if (m_released) {
        ring = std::min(ring, ReleasedRingSeconds);
        overtones = std::min(overtones, ReleasedOvertoneSeconds);
    }
    overtones = std::min(overtones, 0.9 * ring);

    // Loss per trip round the loop at the fundamental and at the overtone frequency.
    const double w0 = 2.0 * std::numbers::pi / m_period;
    const double highHz = std::min(std::max(2000.0, 4.0 * hz), 0.4 * rate);
    const double wh = 2.0 * std::numbers::pi * highHz / rate;
    const double lowGain = std::pow(0.001, m_period / (rate * ring));
    const double highGain = std::pow(0.001, m_period / (rate * overtones));
    // A one-pole low-pass (1 - a) / (1 - a z^-1) whose gain falls by r from DC to wh.
    const double r = highGain / lowGain;
    double a = 0.0;
    if (r < 1.0) {
        const double A = r * r - 1.0, B = 2.0 - 2.0 * r * r * std::cos(wh);
        const double disc = std::max(0.0, B * B - 4.0 * A * A);
        const double r1 = (-B + std::sqrt(disc)) / (2.0 * A), r2 = (-B - std::sqrt(disc)) / (2.0 * A);
        a = (r1 >= 0.0 && r1 < 1.0) ? r1 : r2;
        a = std::clamp(a, 0.0, 0.995);
    }
    // Keep the filter's delay well inside the period (very high, very damped notes).
    a = std::min(a, (0.25 * m_period) / (1.0 + 0.25 * m_period));
    const std::complex<double> z1 = std::polar(1.0, -w0);
    const auto lossAt = (1.0 - a) / (1.0 - a * z1);
    m_lossCoef = a;
    m_gain = std::min(lowGain / std::abs(lossAt), 0.99999);

    m_allpassCoef = Stiffness;
    const auto allpassAt = (m_allpassCoef + z1) / (1.0 + m_allpassCoef * z1);
    const double filterDelay = (-std::arg(lossAt) - std::arg(allpassAt)) / w0;
    m_delay = std::max(2.0, m_period - filterDelay);

    pickupTaps(m_period, m_pitch - m_openKey, c.pickupBlend, m_neck, m_bridge);
    m_neck.delay = std::clamp(m_neck.delay, 1.0, double(m_mask - 4));
    m_bridge.delay = std::clamp(m_bridge.delay, 1.0, double(m_mask - 4));
}

float BassString::read(double delay) const noexcept
{
    // Catmull-Rom between the samples at whole delays i-1 .. i+2 (y[n - i] = m_line[write - i]).
    const int i = int(delay);
    const double t = delay - i;
    const auto at = [this](int d) { return double(m_line[std::size_t((m_write - d) & m_mask)]); };
    const double ym1 = at(i - 1), y0 = at(i), y1 = at(i + 1), y2 = at(i + 2);
    const double c1 = 0.5 * (y1 - ym1);
    const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
    const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    return float(((c3 * t + c2) * t + c1) * t + y0);
}

void BassString::render(float* out, int frames, const StringControls& c) noexcept
{
    if (!m_active) return;

    // Slides move the pitch along; the loop is designed again for the block.
    if (m_pitch != m_target) {
        const double move = m_slideStep * frames;
        m_pitch = m_pitch < m_target ? std::min(m_target, m_pitch + move) : std::max(m_target, m_pitch - move);
    }
    design(c);

    double peak = 0.0;
    for (int n = 0; n < frames; ++n) {
        // read(d) counts back from the newest sample written; before writing this sample's,
        // y[n - D] is D - 1 back from it.
        const double back = read(m_delay - 1.0);
        m_lossState = (1.0 - m_lossCoef) * back + m_lossCoef * m_lossState;
        const double lossy = m_gain * m_lossState;
        const double stiff = m_allpassCoef * lossy + m_allpassIn - m_allpassCoef * m_allpassOut;
        m_allpassIn = lossy;
        m_allpassOut = stiff;

        float input = 0.0f, direct = 0.0f;
        m_exciter.next(input, direct);
        const float y = float(stiff) + input;
        m_write = (m_write + 1) & m_mask;
        m_line[std::size_t(m_write)] = y;

        const double heard = m_neck.gain * (y - read(m_neck.delay)) + m_bridge.gain * (y - read(m_bridge.delay))
                           + direct;
        double sample = heard * m_fade;
        if (m_fadeStep > 0.0) {
            m_fade -= m_fadeStep;
            if (m_fade <= 0.0) { stop(); return; }
        }
        out[n] += float(sample);
        peak = std::max(peak, std::abs(sample));
    }
    m_peak = peak;

    // Done once it has been quiet for two periods and a little more: the first wave takes a
    // whole period to come round, and the pickups can sit near a node for a moment.
    if (!m_exciter.active() && peak < QuietLevel) {
        m_quietFrames += frames;
        if (m_quietFrames > 2.0 * m_period + 0.01 * c.sampleRate) stop();
    } else {
        m_quietFrames = 0;
    }
}

} // namespace bass
