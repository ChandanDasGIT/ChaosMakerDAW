#include "chaoseq.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double FadeSeconds = 0.010;
constexpr double GlideSeconds = 0.020;

double toward(double value, double target, double step) noexcept
{
    return value < target ? std::min(target, value + step) : std::max(target, value - step);
}

double dbToGain(double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

} // namespace

ChaosEq::ChaosEq()
{
    for (int b = 0; b < chaoseq::MaxBands; ++b) {
        const std::string id = "band" + std::to_string(b + 1) + ".";
        auto& p = m_bands[static_cast<std::size_t>(b)];
        p.inUse = &addParameter(id + "inUse", id + "inUse", 0.0f, 1.0f, 0.0f);
        p.bypassed = &addParameter(id + "bypass", id + "bypass", 0.0f, 1.0f, 0.0f);
        p.solo = &addParameter(id + "solo", id + "solo", 0.0f, 1.0f, 0.0f);
        p.placement = &addParameter(id + "placement", id + "placement", 0.0f, 4.0f, 0.0f);
        p.shape = &addParameter(id + "shape", id + "shape", 0.0f, 7.0f, 0.0f);
        p.frequency = &addParameter(id + "frequency", id + "frequency", float(eqband::MinFrequencyHz),
                                    float(eqband::MaxFrequencyHz), 1000.0f, "Hz");
        p.gain = &addParameter(id + "gain", id + "gain", float(eqband::MinGainDb), float(eqband::MaxGainDb), 0.0f, "dB");
        p.q = &addParameter(id + "q", id + "q", float(eqband::MinQ), float(eqband::MaxQ), float(eqband::DefaultQ));
        p.slope = &addParameter(id + "slope", id + "slope", 0.0f, 3.0f, 1.0f);
    }
    m_output = &addParameter("output", "Output", float(chaoseq::MinOutputDb), float(chaoseq::MaxOutputDb), 0.0f, "dB");
    m_autoGain = &addParameter("autoGain", "Auto gain", 0.0f, 1.0f, 0.0f);
    m_autoGainDb = &addParameter("autoGainDb", "Auto gain amount", float(chaoseq::MinOutputDb),
                                 float(chaoseq::MaxOutputDb), 0.0f, "dB");
}

void ChaosEq::prepare(double sampleRate, int)
{
    if (std::isfinite(sampleRate) && sampleRate > 1000.0) m_sampleRate = sampleRate;
    reset();
}

void ChaosEq::reset() noexcept
{
    // Start from the current settings with no fades: a freshly prepared EQ sounds right at once.
    for (auto& voice : m_voices) voice = Voice{};
    m_soloVoice = Voice{};
    const double compensation = m_autoGain->get() > 0.5f ? m_autoGainDb->get() : 0.0;
    m_outputGain = dbToGain(m_output->get() + compensation);
    for (int b = 0; b < chaoseq::MaxBands; ++b) {
        auto& voice = m_voices[static_cast<std::size_t>(b)];
        const Target target = bandTarget(b);
        advance(voice, target, 0);
        if (target.on) voice.mix = 1.0;
    }
}

void ChaosEq::setSettings(const chaoseq::Settings& input) noexcept
{
    const auto s = chaoseq::sanitize(input, m_sampleRate);
    for (int b = 0; b < chaoseq::MaxBands; ++b) {
        const auto& band = s.bands[static_cast<std::size_t>(b)];
        auto& p = m_bands[static_cast<std::size_t>(b)];
        p.inUse->set(band.inUse ? 1.0f : 0.0f);
        p.bypassed->set(band.bypassed ? 1.0f : 0.0f);
        p.solo->set(band.solo ? 1.0f : 0.0f);
        p.placement->set(float(static_cast<int>(band.placement)));
        p.shape->set(float(static_cast<int>(band.filter.shape)));
        p.frequency->set(float(band.filter.frequencyHz));
        p.gain->set(float(band.filter.gainDb));
        p.q->set(float(band.filter.q));
        p.slope->set(float(static_cast<int>(band.filter.slope)));
    }
    m_output->set(float(s.outputGainDb));
    m_autoGain->set(s.autoGain ? 1.0f : 0.0f);
    m_autoGainDb->set(float(chaoseq::autoGainDb(s)));
}

std::size_t ChaosEq::readAnalyzer(std::span<float> out) noexcept
{
    const std::size_t count = std::min(out.size(), m_analyzer.availableToRead());
    return count > 0 && m_analyzer.pop(out.first(count)) ? count : 0;
}

chaoseq::Settings ChaosEq::settings() const noexcept
{
    chaoseq::Settings s;
    for (int b = 0; b < chaoseq::MaxBands; ++b) {
        auto& band = s.bands[static_cast<std::size_t>(b)];
        const auto& p = m_bands[static_cast<std::size_t>(b)];
        band.inUse = p.inUse->get() > 0.5f;
        band.bypassed = p.bypassed->get() > 0.5f;
        band.solo = p.solo->get() > 0.5f;
        band.placement = static_cast<chaoseq::Placement>(std::lround(p.placement->get()));
        band.filter.shape = static_cast<eqband::Shape>(std::lround(p.shape->get()));
        band.filter.frequencyHz = p.frequency->get();
        band.filter.gainDb = p.gain->get();
        band.filter.q = p.q->get();
        band.filter.slope = static_cast<eqband::Slope>(std::lround(p.slope->get()));
    }
    s.outputGainDb = m_output->get();
    s.autoGain = m_autoGain->get() > 0.5f;
    return chaoseq::sanitize(s, m_sampleRate);
}

ChaosEq::Target ChaosEq::bandTarget(int band) const noexcept
{
    const auto& p = m_bands[static_cast<std::size_t>(band)];
    Target t;
    t.source = band;
    t.on = p.inUse->get() > 0.5f && p.bypassed->get() < 0.5f;
    t.shape = int(std::lround(p.shape->get()));
    t.slope = int(std::lround(p.slope->get()));
    t.placement = int(std::lround(p.placement->get()));
    t.frequency = std::min<double>(p.frequency->get(), m_sampleRate * 0.49);
    t.gain = p.gain->get();
    t.q = p.q->get();
    return t;
}

ChaosEq::Ramp ChaosEq::advance(Voice& voice, const Target& t, int frames) noexcept
{
    const bool structureDiffers = voice.source != t.source || voice.shape != t.shape || voice.slope != t.slope
                               || voice.placement != t.placement;
    Ramp ramp{voice.mix, voice.mix};

    // Silent and staying silent: nothing to compute. The next start snaps to the target.
    if (voice.mix <= 0.0 && !t.on) {
        voice.mix = 0.0;
        return {0.0, 0.0};
    }

    bool redesign = false;
    if (voice.mix <= 0.0) {
        // Starting (or switching structure while silent): take the target as it is.
        voice.source = t.source;
        voice.shape = t.shape;
        voice.slope = t.slope;
        voice.placement = t.placement;
        voice.frequency = t.frequency;
        voice.gain = t.gain;
        voice.q = t.q;
        for (auto& channel : voice.state) channel = {};
        redesign = true;
    } else if (!structureDiffers && frames > 0) {
        const double a = 1.0 - std::exp(-frames / (GlideSeconds * m_sampleRate));
        const auto glideLog = [a](double& value, double target) {
            if (value == target) return false;
            value = std::exp(std::log(value) + a * (std::log(target) - std::log(value)));
            if (std::abs(value / target - 1.0) < 1e-5) value = target;
            return true;
        };
        bool moved = glideLog(voice.frequency, t.frequency);
        moved = glideLog(voice.q, t.q) || moved;
        if (voice.gain != t.gain) {
            voice.gain += a * (t.gain - voice.gain);
            if (std::abs(voice.gain - t.gain) < 1e-4) voice.gain = t.gain;
            moved = true;
        }
        redesign = moved;
    }

    const bool sameStructure = voice.source == t.source && voice.shape == t.shape && voice.slope == t.slope
                            && voice.placement == t.placement;
    const double mixTarget = t.on && sameStructure ? 1.0 : 0.0;
    const double step = frames / (FadeSeconds * m_sampleRate);
    voice.mix = frames > 0 ? toward(voice.mix, mixTarget, step) : voice.mix;
    ramp.to = voice.mix;

    if (redesign) {
        eqband::Settings s;
        s.shape = static_cast<eqband::Shape>(voice.shape);
        s.slope = static_cast<eqband::Slope>(voice.slope);
        s.frequencyHz = voice.frequency;
        s.gainDb = voice.gain;
        s.q = voice.q;
        voice.design = eqband::design(s, m_sampleRate);
    }
    if (voice.mix <= 0.0) for (auto& channel : voice.state) channel = {};
    return ramp;
}

void ChaosEq::runVoice(Voice& voice, Ramp mix, double (*buffer)[SubBlock], int channels, int frames) noexcept
{
    const auto& design = voice.design;
    const auto filter = [&design](std::array<biquad::State, eqband::MaxSections>& state, double x) {
        for (int k = 0; k < design.sectionCount; ++k)
            x = biquad::process(design.sections[static_cast<std::size_t>(k)], state[static_cast<std::size_t>(k)], x);
        return x;
    };
    const auto placement = static_cast<chaoseq::Placement>(voice.placement);
    for (int i = 0; i < frames; ++i) {
        const double m = mix.from + (mix.to - mix.from) * (i + 1) / frames;
        if (placement == chaoseq::Placement::Stereo) {
            for (int c = 0; c < channels; ++c) {
                const double x = buffer[c][i];
                buffer[c][i] = x + m * (filter(voice.state[static_cast<std::size_t>(c)], x) - x);
            }
        } else if (placement == chaoseq::Placement::Left || placement == chaoseq::Placement::Right) {
            const int c = placement == chaoseq::Placement::Left ? 0 : 1;
            if (c >= channels) return;
            const double x = buffer[c][i];
            buffer[c][i] = x + m * (filter(voice.state[0], x) - x);
        } else if (channels < 2) {
            if (placement == chaoseq::Placement::Side) return;   // mono has no side
            const double x = buffer[0][i];
            buffer[0][i] = x + m * (filter(voice.state[0], x) - x);
        } else {
            double mid = 0.5 * (buffer[0][i] + buffer[1][i]);
            double side = 0.5 * (buffer[0][i] - buffer[1][i]);
            double& part = placement == chaoseq::Placement::Mid ? mid : side;
            part = part + m * (filter(voice.state[0], part) - part);
            buffer[0][i] = mid + side;
            buffer[1][i] = mid - side;
        }
    }
}

void ChaosEq::process(ProcessContext& context) noexcept
{
    const int channels = std::min(context.audio.channelCount, 2);
    if (channels <= 0) return;

    // Read the settings once per block.
    std::array<Target, chaoseq::MaxBands> targets;
    int soloing = -1;
    for (int b = 0; b < chaoseq::MaxBands; ++b) {
        targets[static_cast<std::size_t>(b)] = bandTarget(b);
        const auto& p = m_bands[static_cast<std::size_t>(b)];
        if (soloing < 0 && targets[static_cast<std::size_t>(b)].on && p.solo->get() > 0.5f) soloing = b;
    }
    // The solo voice is a band pass over the soloed band's region; a new solo band fades the
    // old one out first (the source band is part of the voice's structure).
    Target soloTarget;
    if (soloing >= 0) {
        const auto& band = targets[static_cast<std::size_t>(soloing)];
        const auto shape = static_cast<eqband::Shape>(band.shape);
        const bool hasWidth = shape == eqband::Shape::Bell || shape == eqband::Shape::Notch
                           || shape == eqband::Shape::BandPass;
        soloTarget.on = true;
        soloTarget.source = soloing;
        soloTarget.shape = static_cast<int>(eqband::Shape::BandPass);
        soloTarget.slope = static_cast<int>(eqband::Slope::Db12);
        soloTarget.placement = static_cast<int>(chaoseq::Placement::Stereo);
        soloTarget.frequency = band.frequency;
        soloTarget.q = std::clamp(hasWidth ? band.q : eqband::DefaultQ, 0.2, eqband::MaxQ);
    } else {
        soloTarget.source = m_soloVoice.source;
        soloTarget.shape = m_soloVoice.shape;
        soloTarget.slope = m_soloVoice.slope;
        soloTarget.placement = m_soloVoice.placement;
    }
    const double compensation = m_autoGain->get() > 0.5f ? m_autoGainDb->get() : 0.0;
    const double outputTarget = dbToGain(m_output->get() + compensation);

    double work[2][SubBlock];
    double dry[2][SubBlock];
    for (int offset = 0; offset < context.audio.frames; offset += SubBlock) {
        const int n = std::min(SubBlock, context.audio.frames - offset);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i)
                dry[c][i] = work[c][i] = context.audio.channel(c)[offset + i];

        for (int b = 0; b < chaoseq::MaxBands; ++b) {
            auto& voice = m_voices[static_cast<std::size_t>(b)];
            const Ramp ramp = advance(voice, targets[static_cast<std::size_t>(b)], n);
            if (!ramp.silent()) runVoice(voice, ramp, work, channels, n);
        }

        // Solo: crossfade from the full EQ to the soloed band's region of the input.
        const Ramp solo = advance(m_soloVoice, soloTarget, n);
        if (!solo.silent()) {
            Ramp full{1.0, 1.0};
            runVoice(m_soloVoice, full, dry, channels, n);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < n; ++i) {
                    const double s = solo.from + (solo.to - solo.from) * (i + 1) / n;
                    work[c][i] += s * (dry[c][i] - work[c][i]);
                }
        }

        const double gainFrom = m_outputGain;
        const double a = 1.0 - std::exp(-n / (GlideSeconds * m_sampleRate));
        m_outputGain += a * (outputTarget - m_outputGain);
        if (std::abs(m_outputGain - outputTarget) < 1e-7) m_outputGain = outputTarget;
        for (int c = 0; c < channels; ++c) {
            float* out = context.audio.channel(c) + offset;
            for (int i = 0; i < n; ++i) {
                const double g = gainFrom + (m_outputGain - gainFrom) * (i + 1) / n;
                out[i] = static_cast<float>(work[c][i] * g);
            }
        }
        if (m_listening.load(std::memory_order_relaxed)) {
            float mono[SubBlock];
            for (int i = 0; i < n; ++i) {
                float sum = 0.0f;
                for (int c = 0; c < channels; ++c) sum += context.audio.channel(c)[offset + i];
                mono[i] = sum / float(channels);
            }
            m_analyzer.push(std::span<const float>(mono, std::size_t(n)));
        }
    }
}
