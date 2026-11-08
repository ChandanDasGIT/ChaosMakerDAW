#include "instruments/electrodrums/electrodrums.h"
#include "instruments/electrodrums/electromap.h"

#include <algorithm>
#include <cmath>

using electrodrums::Drum;

namespace {

double dbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }

} // namespace

ElectroDrums::ElectroDrums()
{
    using namespace electrodrums;
    m_p.style = &addParameter("style", "Style", 0, 2, 0);
    m_p.volume = &addParameter("volume", "Volume", float(MinLevelDb), float(MaxLevelDb), 0, "dB");
    for (int i = 0; i < DrumCount; ++i) {
        const std::string id(drumId(Drum(i)));
        const std::string title(drumTitle(Drum(i)));
        auto& d = m_p.drums[std::size_t(i)];
        d.tune = &addParameter(id + "Tune", title + " tune", float(MinTune), float(MaxTune), 0, "semitones");
        d.decay = &addParameter(id + "Decay", title + " decay", float(MinDecay), float(MaxDecay), 1);
        d.level = &addParameter(id + "Level", title + " level", float(MinLevelDb), float(MaxLevelDb), 0, "dB");
    }
    prepare(48000.0, 1024);
}

void ElectroDrums::setSettings(const electrodrums::Settings& input) noexcept
{
    const auto s = electrodrums::sanitize(input);
    m_p.style->set(float(int(s.style)));
    m_p.volume->set(float(s.volumeDb));
    for (std::size_t i = 0; i < m_p.drums.size(); ++i) {
        m_p.drums[i].tune->set(float(s.drums[i].tuneSemitones));
        m_p.drums[i].decay->set(float(s.drums[i].decay));
        m_p.drums[i].level->set(float(s.drums[i].levelDb));
    }
}

electrodrums::Settings ElectroDrums::settings() const noexcept
{
    electrodrums::Settings s;
    s.style = electrodrums::Style(std::lround(m_p.style->get()));
    s.volumeDb = m_p.volume->get();
    for (std::size_t i = 0; i < m_p.drums.size(); ++i)
        s.drums[i] = {m_p.drums[i].tune->get(), m_p.drums[i].decay->get(), m_p.drums[i].level->get()};
    return s;
}

ElectroDrums::Hits ElectroDrums::hits(int key) const noexcept
{
    if (key < 0 || key > 127) return {};
    const auto count = m_hitCount[std::size_t(key)].load(std::memory_order_acquire);
    return {count, m_hitVelocity[std::size_t(key)].load(std::memory_order_relaxed)};
}

void ElectroDrums::prepare(double sampleRate, int maxFrames)
{
    (void)maxFrames;                                       // works in its own short blocks
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void ElectroDrums::reset() noexcept
{
    for (auto& v : m_voices) v = electrodrums::Voice{};
    m_started = {};
}

float ElectroDrums::velocityGain(int velocity) noexcept
{
    const double v = std::clamp(velocity, 0, 127) / 127.0;
    return float(std::pow(v, 1.6));
}

int ElectroDrums::activeVoices() const noexcept
{
    return int(std::count_if(m_voices.begin(), m_voices.end(), [](const auto& v) { return v.active(); }));
}

electrodrums::Voice& ElectroDrums::freeVoice() noexcept
{
    // A silent voice; else the oldest one already fading out (choked); else the oldest.
    std::size_t best = 0;
    int bestRank = 3;
    for (std::size_t i = 0; i < m_voices.size(); ++i) {
        const auto& v = m_voices[i];
        const int rank = !v.active() ? 0 : v.choking() ? 1 : 2;
        if (rank < bestRank || (rank == bestRank && m_started[i] < m_started[best])) {
            best = i;
            bestRank = rank;
        }
        if (rank == 0) break;
    }
    m_started[best] = ++m_hits;
    return m_voices[best];
}

void ElectroDrums::noteOn(int key, int velocity) noexcept
{
    const auto* pad = electromap::find(key);
    if (!pad) return;
    const Drum drum = pad->drum;
    const bool closesHat = drum == Drum::HatClosed || drum == Drum::HatPedal;
    for (auto& v : m_voices) {
        if (!v.active()) continue;
        if (v.drum() == drum || (closesHat && v.drum() == Drum::HatOpen)) v.choke();
    }
    const auto& p = m_p.drums[std::size_t(drum)];
    electrodrums::Knobs knobs;
    knobs.tuneSemitones = p.tune->get();
    knobs.decay = p.decay->get() * pad->decayScale;
    const auto style = electrodrums::Style(std::clamp(int(std::lround(m_p.style->get())), 0, 2));
    const float gain = velocityGain(velocity) * float(dbToGain(p.level->get() + m_p.volume->get()));
    m_seed = m_seed * 1664525u + 1013904223u;
    freeVoice().start(drum, style, knobs, gain, m_sampleRate, m_seed);
    m_hitVelocity[std::size_t(key)].store(std::uint8_t(velocity), std::memory_order_relaxed);
    m_hitCount[std::size_t(key)].fetch_add(1, std::memory_order_release);
}

void ElectroDrums::handle(const MidiEvent& e) noexcept
{
    const int type = e.status & 0xF0;
    if (type == 0x90 && e.data2 > 0) noteOn(e.data1 & 0x7F, e.data2);
    else if (type == 0xB0 && e.data1 == 120)               // all sound off: cut every hit
        for (auto& v : m_voices) v.choke();
    // Note-off and all-notes-off (123) leave one-shots to ring out.
}

void ElectroDrums::renderRange(const AudioView& audio, int start, int end) noexcept
{
    while (start < end) {
        const int frames = std::min(Block, end - start);
        std::array<float, Block> mono{};
        for (auto& v : m_voices) v.render(mono.data(), frames);
        for (int c = 0; c < audio.channelCount; ++c) {
            float* out = audio.channel(c) + start;
            for (int i = 0; i < frames; ++i) out[i] += mono[std::size_t(i)];
        }
        start += frames;
    }
}

void ElectroDrums::process(ProcessContext& context) noexcept
{
    const auto& audio = context.audio;
    if (audio.frames <= 0 || audio.channelCount <= 0 || !audio.channels) return;
    int frame = 0;
    if (context.midi) {
        for (const auto& event : context.midi->events()) {
            const int at = std::clamp(event.offset, 0, audio.frames);
            if (at > frame) { renderRange(audio, frame, at); frame = at; }
            handle(event);
        }
    }
    renderRange(audio, frame, audio.frames);
}
