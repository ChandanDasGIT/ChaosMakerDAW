#include "instruments/electrodrums/electrovoices.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace electrodrums {

namespace {

constexpr double TwoPi = 2.0 * std::numbers::pi;

// The classic analogue cymbal oscillators (808 style) and a brighter, denser set (909 style).
constexpr std::array<double, 6> Metal808{205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
constexpr std::array<double, 6> Metal909{263.0, 400.0, 421.0, 474.0, 587.0, 845.0};

// Multiplier per sample that falls to -80 dB in `seconds`.
double decayCoef(double seconds, double rate) noexcept
{
    if (seconds <= 0.0) return 0.0;
    return std::exp(std::log(1e-4) / (seconds * rate));
}

// PolyBLEP correction: removes most of the aliasing of a square wave's jumps.
double blep(double t, double dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

Recipe recipe808(Drum drum)
{
    Recipe r;
    switch (drum) {
    case Drum::Kick:
        r.tones[0] = {49.0, 2.6, 0.025, 1.4, 0.95};
        r.noises[0] = {FilterMode::HighPass, 3000.0, 0.707, 0.006, 0.12};
        r.drive = 1.2;
        break;
    case Drum::SideStick:
        r.tones[0] = {455.0, 1.0, 0.0, 0.035, 0.5};
        r.tones[1] = {1667.0, 1.0, 0.0, 0.03, 0.35};
        r.noises[0] = {FilterMode::HighPass, 2500.0, 0.707, 0.01, 0.15};
        r.drive = 1.8;
        break;
    case Drum::Snare:
        r.tones[0] = {175.0, 1.05, 0.01, 0.16, 0.45};
        r.tones[1] = {330.0, 1.05, 0.01, 0.11, 0.3};
        r.noises[0] = {FilterMode::HighPass, 1800.0, 0.707, 0.22, 0.5};
        r.drive = 1.2;
        break;
    case Drum::Clap:
        r.noises[0] = {FilterMode::BandPass, 1100.0, 1.4, 0.28, 1.6, 4, 0.011, 0.012};
        r.drive = 1.3;
        break;
    case Drum::TomLow: case Drum::TomMid: case Drum::TomHigh: {
        const double f = drum == Drum::TomLow ? 88.0 : drum == Drum::TomMid ? 125.0 : 175.0;
        r.tones[0] = {f, 1.12, 0.06, 0.55, 0.85};
        r.noises[0] = {FilterMode::HighPass, 2000.0, 0.707, 0.008, 0.1};
        break;
    }
    case Drum::HatClosed: case Drum::HatPedal: case Drum::HatOpen:
        r.metal.frequencies = Metal808;
        r.metal.bandPass = 10000.0; r.metal.bandQ = 0.8; r.metal.highPass = 7000.0;
        r.metal.noise = 0.1;
        r.metal.decaySeconds = drum == Drum::HatClosed ? 0.07 : drum == Drum::HatPedal ? 0.045 : 0.7;
        r.metal.gain = drum == Drum::HatPedal ? 1.9 : 2.4;
        break;
    case Drum::Crash:
        r.metal.frequencies = Metal808;
        r.metal.bandPass = 7000.0; r.metal.bandQ = 0.5; r.metal.highPass = 4500.0;
        r.metal.noise = 0.4; r.metal.decaySeconds = 2.0; r.metal.gain = 1.4;
        break;
    case Drum::Ride:
        r.metal.frequencies = Metal808;
        r.metal.bandPass = 5500.0; r.metal.bandQ = 1.5; r.metal.highPass = 3500.0;
        r.metal.noise = 0.15; r.metal.decaySeconds = 2.6; r.metal.gain = 1.1;
        r.tones[0] = {620.0, 1.0, 0.0, 0.6, 0.2};
        break;
    case Drum::Count: break;
    }
    return r;
}

Recipe recipe909(Drum drum)
{
    Recipe r;
    switch (drum) {
    case Drum::Kick:
        r.tones[0] = {54.0, 4.5, 0.018, 0.55, 0.75};
        r.noises[0] = {FilterMode::HighPass, 1500.0, 0.707, 0.012, 0.3};
        r.drive = 2.0;
        break;
    case Drum::SideStick:
        r.tones[0] = {480.0, 1.1, 0.002, 0.03, 0.45};
        r.tones[1] = {1750.0, 1.0, 0.0, 0.025, 0.3};
        r.noises[0] = {FilterMode::BandPass, 3000.0, 1.2, 0.02, 0.5};
        r.drive = 2.0;
        break;
    case Drum::Snare:
        r.tones[0] = {190.0, 1.4, 0.008, 0.13, 0.5};
        r.tones[1] = {345.0, 1.3, 0.008, 0.09, 0.25};
        r.noises[0] = {FilterMode::HighPass, 2200.0, 0.707, 0.3, 0.75};
        r.drive = 1.5;
        break;
    case Drum::Clap:
        r.noises[0] = {FilterMode::BandPass, 1350.0, 1.2, 0.2, 1.6, 4, 0.009, 0.009};
        r.drive = 1.3;
        break;
    case Drum::TomLow: case Drum::TomMid: case Drum::TomHigh: {
        const double f = drum == Drum::TomLow ? 100.0 : drum == Drum::TomMid ? 145.0 : 205.0;
        r.tones[0] = {f, 1.6, 0.035, 0.42, 0.85};
        r.noises[0] = {FilterMode::BandPass, 1800.0, 0.8, 0.03, 0.25};
        r.drive = 1.6;
        break;
    }
    case Drum::HatClosed: case Drum::HatPedal: case Drum::HatOpen:
        r.metal.frequencies = Metal909;
        r.metal.bandPass = 11000.0; r.metal.bandQ = 0.7; r.metal.highPass = 8000.0;
        r.metal.noise = 0.35;
        r.metal.decaySeconds = drum == Drum::HatClosed ? 0.055 : drum == Drum::HatPedal ? 0.04 : 0.5;
        r.metal.gain = drum == Drum::HatPedal ? 1.8 : 2.4;
        break;
    case Drum::Crash:
        r.metal.frequencies = Metal909;
        r.metal.bandPass = 8000.0; r.metal.bandQ = 0.5; r.metal.highPass = 5000.0;
        r.metal.noise = 0.6; r.metal.decaySeconds = 2.2; r.metal.gain = 1.5;
        break;
    case Drum::Ride:
        r.metal.frequencies = Metal909;
        r.metal.bandPass = 6500.0; r.metal.bandQ = 1.4; r.metal.highPass = 4000.0;
        r.metal.noise = 0.25; r.metal.decaySeconds = 3.0; r.metal.gain = 1.2;
        r.tones[0] = {700.0, 1.0, 0.0, 0.5, 0.18};
        break;
    case Drum::Count: break;
    }
    return r;
}

// Studio: nearer an acoustic kit. The kick is shorter and gets a shell tone and a beater click,
// the snare a fuller head and longer wires, toms an overtone, the metal is mostly noise.
Recipe recipeStudio(Drum drum)
{
    Recipe r;
    switch (drum) {
    case Drum::Kick:
        r.tones[0] = {58.0, 2.0, 0.012, 0.35, 0.5};            // the low end, tight
        r.tones[1] = {140.0, 1.3, 0.008, 0.18, 0.55};          // the shell's body
        r.noises[0] = {FilterMode::BandPass, 3200.0, 0.9, 0.025, 0.75};   // beater
        r.noises[1] = {FilterMode::LowPass, 600.0, 0.707, 0.05, 0.25};    // the thump of air
        r.drive = 1.6;
        break;
    case Drum::SideStick:
        r.tones[0] = {520.0, 1.05, 0.002, 0.04, 0.35};
        r.tones[1] = {1300.0, 1.0, 0.0, 0.03, 0.2};
        r.noises[0] = {FilterMode::BandPass, 2200.0, 1.5, 0.035, 0.7};    // the wood of the stick
        r.drive = 1.4;
        break;
    case Drum::Snare:
        r.tones[0] = {200.0, 1.15, 0.006, 0.16, 0.45};
        r.tones[1] = {330.0, 1.1, 0.006, 0.1, 0.25};
        r.noises[0] = {FilterMode::BandPass, 4500.0, 0.5, 0.32, 0.8};     // the wires
        r.noises[1] = {FilterMode::HighPass, 1200.0, 0.707, 0.12, 0.35};  // the crack
        r.drive = 1.5;
        break;
    case Drum::Clap:
        r.noises[0] = {FilterMode::BandPass, 1600.0, 0.9, 0.22, 1.3, 3, 0.008, 0.007};
        r.noises[1] = {FilterMode::HighPass, 3000.0, 0.707, 0.08, 0.3};
        r.drive = 1.3;
        break;
    case Drum::TomLow: case Drum::TomMid: case Drum::TomHigh: {
        const double f = drum == Drum::TomLow ? 95.0 : drum == Drum::TomMid ? 135.0 : 185.0;
        r.tones[0] = {f, 1.25, 0.03, 0.6, 0.65};
        r.tones[1] = {f * 1.58, 1.2, 0.02, 0.25, 0.28};        // the shell's first overtone
        r.noises[0] = {FilterMode::BandPass, 2500.0, 0.8, 0.02, 0.35};    // the stick
        r.drive = 1.4;
        break;
    }
    case Drum::HatClosed: case Drum::HatPedal: case Drum::HatOpen:
        r.metal.frequencies = Metal909;
        r.metal.bandPass = 9000.0; r.metal.bandQ = 0.6; r.metal.highPass = 6500.0;
        r.metal.noise = 0.9;
        r.metal.decaySeconds = drum == Drum::HatClosed ? 0.09 : drum == Drum::HatPedal ? 0.06 : 0.8;
        r.metal.gain = drum == Drum::HatPedal ? 0.9 : 1.1;
        break;
    case Drum::Crash:
        r.metal.frequencies = Metal808;
        r.metal.bandPass = 6000.0; r.metal.bandQ = 0.4; r.metal.highPass = 3500.0;
        r.metal.noise = 1.0; r.metal.decaySeconds = 2.4; r.metal.gain = 0.8;
        break;
    case Drum::Ride:
        r.metal.frequencies = Metal909;
        r.metal.bandPass = 5000.0; r.metal.bandQ = 1.2; r.metal.highPass = 3000.0;
        r.metal.noise = 0.5; r.metal.decaySeconds = 3.2; r.metal.gain = 1.1;
        r.tones[0] = {760.0, 1.0, 0.0, 0.9, 0.22};             // the bell's ping
        break;
    case Drum::Count: break;
    }
    return r;
}

} // namespace

std::string_view drumId(Drum drum) noexcept
{
    switch (drum) {
    case Drum::Kick: return "kick";
    case Drum::SideStick: return "sideStick";
    case Drum::Snare: return "snare";
    case Drum::Clap: return "clap";
    case Drum::TomLow: return "tomLow";
    case Drum::TomMid: return "tomMid";
    case Drum::TomHigh: return "tomHigh";
    case Drum::HatClosed: return "hihatClosed";
    case Drum::HatPedal: return "hihatPedal";
    case Drum::HatOpen: return "hihatOpen";
    case Drum::Crash: return "crash";
    case Drum::Ride: return "ride";
    case Drum::Count: break;
    }
    return {};
}

std::string_view styleId(Style style) noexcept
{
    switch (style) {
    case Style::Style808: return "808";
    case Style::Style909: return "909";
    case Style::Studio: return "studio";
    }
    return {};
}

double Recipe::lengthSeconds() const noexcept
{
    double length = metal.decaySeconds;
    for (const auto& t : tones)
        if (t.frequency > 0.0) length = std::max(length, t.decaySeconds);
    for (const auto& n : noises)
        length = std::max(length, (n.bursts - 1) * n.burstSpacingSeconds + n.decaySeconds);
    return length;
}

Recipe recipe(Drum drum, Style style, const Knobs& knobs) noexcept
{
    Recipe r = style == Style::Style808 ? recipe808(drum)
             : style == Style::Style909 ? recipe909(drum) : recipeStudio(drum);
    const double ratio = std::exp2(std::clamp(knobs.tuneSemitones, MinTune, MaxTune) / 12.0);
    const double stretch = std::clamp(knobs.decay, MinDecay, MaxDecay);
    for (auto& t : r.tones) {
        t.frequency *= ratio;
        t.decaySeconds *= stretch;
    }
    for (auto& n : r.noises) {
        n.frequency *= ratio;
        n.decaySeconds *= stretch;
    }
    for (auto& f : r.metal.frequencies) f *= ratio;
    r.metal.bandPass *= ratio;
    r.metal.highPass *= ratio;
    r.metal.decaySeconds *= stretch;
    return r;
}

void Voice::Svf::set(double frequency, double q, double sampleRate) noexcept
{
    // Topology-preserving state-variable filter (trapezoidal integrators).
    const double f = std::clamp(frequency, 10.0, 0.45 * sampleRate);
    const double g = std::tan(std::numbers::pi * f / sampleRate);
    k = 1.0 / std::max(0.1, q);
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    ic1 = ic2 = 0.0;
}

double Voice::Svf::process(double in, FilterMode mode) noexcept
{
    const double v3 = in - ic2;
    const double v1 = a1 * ic1 + a2 * v3;
    const double v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    switch (mode) {
    case FilterMode::LowPass: return v2;
    case FilterMode::BandPass: return k * v1;          // 0 dB at the centre
    case FilterMode::HighPass: return in - k * v1 - v2;
    }
    return in;
}

double Voice::noise() noexcept
{
    // xorshift32
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return double(m_seed) / 2147483648.0 - 1.0;
}

void Voice::start(Drum drum, Style style, const Knobs& knobs, float gain, double sampleRate,
                  std::uint32_t seed) noexcept
{
    m_drum = drum;
    m_rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_recipe = recipe(drum, style, knobs);
    m_gain = gain;
    m_seed = seed ? seed : 1u;
    m_samplesLeft = long(std::ceil(m_recipe.lengthSeconds() * m_rate));
    m_active = m_samplesLeft > 0;
    m_chokeLeft = -1;
    m_chokeSamples = std::max(1L, long(ChokeSeconds * m_rate));

    for (std::size_t i = 0; i < m_tones.size(); ++i) {
        const auto& t = m_recipe.tones[i];
        auto& s = m_tones[i];
        s.phase = 0.0;
        s.sweep = t.sweepSeconds > 0.0 ? 1.0 : 0.0;
        s.sweepCoef = t.sweepSeconds > 0.0 ? std::exp(-1.0 / (t.sweepSeconds * m_rate)) : 0.0;
        s.amp = t.frequency > 0.0 ? 1.0 : 0.0;
        s.ampCoef = decayCoef(t.decaySeconds, m_rate);
    }
    for (std::size_t i = 0; i < m_noises.size(); ++i) {
        const auto& n = m_recipe.noises[i];
        auto& s = m_noises[i];
        s.filter.set(n.frequency, n.q, m_rate);
        s.amp = n.decaySeconds > 0.0 ? 1.0 : 0.0;
        s.ampCoef = decayCoef(n.decaySeconds, m_rate);
        s.burstCoef = decayCoef(n.burstDecaySeconds, m_rate);
        s.burstsLeft = std::max(0, n.bursts - 1);
        s.burstSpacing = std::max(1L, long(n.burstSpacingSeconds * m_rate));
        s.untilNext = s.burstSpacing;
    }
    m_metalPhase = {};
    m_metalBand.set(m_recipe.metal.bandPass, m_recipe.metal.bandQ, m_rate);
    m_metalHigh.set(m_recipe.metal.highPass, 0.707, m_rate);
    m_metalAmp = m_recipe.metal.decaySeconds > 0.0 ? 1.0 : 0.0;
    m_metalCoef = decayCoef(m_recipe.metal.decaySeconds, m_rate);
    m_driveNorm = m_recipe.drive > 1.0 ? 1.0 / std::tanh(m_recipe.drive) : 1.0;
}

void Voice::choke() noexcept
{
    if (m_active && m_chokeLeft < 0) m_chokeLeft = m_chokeSamples;
}

void Voice::render(float* mono, int frames) noexcept
{
    if (!m_active) return;
    const auto& metal = m_recipe.metal;
    const bool hasMetal = m_metalAmp > 0.0;
    for (int i = 0; i < frames; ++i) {
        double out = 0.0;
        for (std::size_t t = 0; t < m_tones.size(); ++t) {
            auto& s = m_tones[t];
            if (s.amp < 1e-6) continue;
            const auto& tone = m_recipe.tones[t];
            out += std::sin(TwoPi * s.phase) * s.amp * tone.gain;
            const double f = tone.frequency * (1.0 + (tone.sweep - 1.0) * s.sweep);
            s.phase += f / m_rate;
            s.phase -= std::floor(s.phase);
            s.sweep *= s.sweepCoef;
            s.amp *= s.ampCoef;
        }
        for (std::size_t n = 0; n < m_noises.size(); ++n) {
            auto& s = m_noises[n];
            if (s.amp < 1e-6 && s.burstsLeft == 0) continue;
            const auto& layer = m_recipe.noises[n];
            out += s.filter.process(noise(), layer.mode) * s.amp * layer.gain;
            const bool inBurst = s.burstsLeft > 0;
            s.amp *= inBurst ? s.burstCoef : s.ampCoef;
            if (inBurst && --s.untilNext <= 0) {         // the next clap burst
                s.amp = 1.0;
                s.untilNext = s.burstSpacing;
                --s.burstsLeft;
            }
        }
        if (hasMetal && m_metalAmp > 1e-6) {
            double square = 0.0;
            for (std::size_t k = 0; k < m_metalPhase.size(); ++k) {
                const double dt = metal.frequencies[k] / m_rate;
                double& p = m_metalPhase[k];
                double v = p < 0.5 ? 1.0 : -1.0;
                v += blep(p, dt);
                double half = p + 0.5; half -= std::floor(half);
                v -= blep(half, dt);
                square += v;
                p += dt; p -= std::floor(p);
            }
            const double raw = square / 6.0 + metal.noise * noise();
            const double band = m_metalBand.process(raw, FilterMode::BandPass);
            out += m_metalHigh.process(band, FilterMode::HighPass) * m_metalAmp * metal.gain;
            m_metalAmp *= m_metalCoef;
        }
        if (m_recipe.drive > 1.0) out = std::tanh(m_recipe.drive * out) * m_driveNorm;
        double gain = m_gain * m_recipe.output;
        if (m_chokeLeft >= 0) gain *= double(m_chokeLeft) / double(m_chokeSamples);
        mono[i] += float(out * gain);

        if (m_chokeLeft >= 0 && --m_chokeLeft < 0) { m_active = false; return; }
        if (--m_samplesLeft <= 0) { m_active = false; return; }
    }
}

} // namespace electrodrums
