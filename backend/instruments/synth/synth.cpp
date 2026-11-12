#include "instruments/synth/synth.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

using namespace synth;

namespace {

// Each setting as one Parameter: a field table drives both setSettings() and settings().
template <typename T>
double toDouble(T value) noexcept
{
    if constexpr (std::is_enum_v<T>) return double(int(value));
    else return double(value);
}

template <typename T>
void assign(T& field, double value) noexcept
{
    if constexpr (std::is_same_v<T, bool>) field = value >= 0.5;
    else if constexpr (std::is_enum_v<T>) field = T(int(std::lround(value)));
    else if constexpr (std::is_integral_v<T>) field = T(std::lround(value));
    else field = T(value);
}

struct Field {
    const char* id;
    const char* name;
    double min, max;
    double (*get)(const Settings&);
    void (*put)(Settings&, double);
};

#define SYNTH_FIELD(ID, NAME, MIN, MAX, EXPR)                                                  \
    Field{ID, NAME, double(MIN), double(MAX),                                                  \
          [](const Settings& s) { return toDouble(s.EXPR); },                                  \
          [](Settings& s, double v) { assign(s.EXPR, v); }}

#define SYNTH_OSC(N, P)                                                                        \
    SYNTH_FIELD(P "On", "Osc " #N " on", 0, 1, osc[N - 1].on),                                 \
    SYNTH_FIELD(P "Wave", "Osc " #N " wave", 0, 3, osc[N - 1].wave),                           \
    SYNTH_FIELD(P "Octave", "Osc " #N " octave", -MaxOctave, MaxOctave, osc[N - 1].octave),    \
    SYNTH_FIELD(P "Semitones", "Osc " #N " semitones", -MaxSemitones, MaxSemitones,            \
                osc[N - 1].semitones),                                                         \
    SYNTH_FIELD(P "Fine", "Osc " #N " fine", -MaxFineCents, MaxFineCents, osc[N - 1].fineCents), \
    SYNTH_FIELD(P "PulseWidth", "Osc " #N " pulse width", MinPulseWidth, MaxPulseWidth,        \
                osc[N - 1].pulseWidth),                                                        \
    SYNTH_FIELD(P "Level", "Osc " #N " level", 0, 1, osc[N - 1].level)

#define SYNTH_ENV(P, LABEL, E)                                                                 \
    SYNTH_FIELD(P "Attack", LABEL " attack", MinEnvSeconds, MaxEnvSeconds, E.attack),          \
    SYNTH_FIELD(P "Decay", LABEL " decay", MinEnvSeconds, MaxEnvSeconds, E.decay),             \
    SYNTH_FIELD(P "Sustain", LABEL " sustain", 0, 1, E.sustain),                               \
    SYNTH_FIELD(P "Release", LABEL " release", MinEnvSeconds, MaxEnvSeconds, E.release)

const auto& fields()
{
    static const Field table[] = {
        SYNTH_OSC(1, "osc1"),
        SYNTH_OSC(2, "osc2"),
        SYNTH_FIELD("subWave", "Sub wave", 0, 1, subWave),
        SYNTH_FIELD("subLevel", "Sub level", 0, 1, subLevel),
        SYNTH_FIELD("noiseLevel", "Noise level", 0, 1, noiseLevel),
        SYNTH_FIELD("filterMode", "Filter mode", 0, 3, filter.mode),
        SYNTH_FIELD("cutoff", "Cutoff", MinCutoffHz, MaxCutoffHz, filter.cutoffHz),
        SYNTH_FIELD("resonance", "Resonance", 0, 1, filter.resonance),
        SYNTH_FIELD("drive", "Drive", 0, 1, filter.drive),
        SYNTH_FIELD("keyTrack", "Key tracking", 0, 1, filter.keyTrack),
        SYNTH_FIELD("filterEnvAmount", "Filter envelope", -MaxFilterEnvOctaves, MaxFilterEnvOctaves,
                    filter.envOctaves),
        SYNTH_FIELD("filterVelocity", "Filter velocity", 0, 1, filter.velocity),
        SYNTH_ENV("filterEnv", "Filter", filterEnv),
        SYNTH_ENV("ampEnv", "Amp", ampEnv),
        SYNTH_FIELD("ampVelocity", "Amp velocity", 0, 1, ampVelocity),
        SYNTH_FIELD("lfoShape", "LFO shape", 0, 4, lfo.shape),
        SYNTH_FIELD("lfoTarget", "LFO target", 0, 3, lfo.target),
        SYNTH_FIELD("lfoRate", "LFO rate", MinLfoHz, MaxLfoHz, lfo.rateHz),
        SYNTH_FIELD("lfoSync", "LFO tempo sync", 0, 1, lfo.sync),
        SYNTH_FIELD("lfoDivision", "LFO division", 0, DivisionCount - 1, lfo.division),
        SYNTH_FIELD("lfoDepth", "LFO depth", 0, 1, lfo.depth),
        SYNTH_FIELD("lfoDelay", "LFO delay", 0, MaxLfoDelaySeconds, lfo.delaySeconds),
        SYNTH_FIELD("playMode", "Play mode", 0, 2, mode),
        SYNTH_FIELD("glide", "Glide", 0, MaxGlideSeconds, glideSeconds),
        SYNTH_FIELD("bendRange", "Bend range", 0, MaxBendSemitones, bendSemitones),
        SYNTH_FIELD("volume", "Volume", MinVolumeDb, MaxVolumeDb, volumeDb),
    };
    return table;
}

#undef SYNTH_ENV
#undef SYNTH_OSC
#undef SYNTH_FIELD

} // namespace

Synth::Synth()
{
    const Settings defaults;
    for (const auto& f : fields())
        m_params.push_back(&addParameter(f.id, f.name, float(f.min), float(f.max), float(f.get(defaults))));
    prepare(48000.0, 1024);
}

void Synth::setSettings(const Settings& input) noexcept
{
    const auto s = sanitize(input);
    std::size_t i = 0;
    for (const auto& f : fields()) m_params[i++]->set(float(f.get(s)));
}

Settings Synth::settings() const noexcept
{
    Settings s;
    std::size_t i = 0;
    for (const auto& f : fields()) f.put(s, m_params[i++]->get());
    return s;
}

void Synth::readSettings() noexcept
{
    const auto before = m_s.mode;
    std::size_t i = 0;
    for (const auto& f : fields()) f.put(m_s, m_params[i++]->get());
    // Between one voice and many, the held notes no longer fit: let them go.
    if ((before == PlayMode::Poly) != (m_s.mode == PlayMode::Poly)) allNotesOff();
}

void Synth::prepare(double sampleRate, int maxFrames)
{
    (void)maxFrames;                                // works in its own short blocks
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void Synth::reset() noexcept
{
    for (auto& v : m_voices) v.stop();
    m_started.fill(0);
    m_sustained.fill(false);
    m_clock = 0;
    m_down.fill(false);
    m_velocity.fill(100);
    m_stackSize = 0;
    m_monoSustained = false;
    m_pedal = false;
    m_bend = 0.0;
    m_modWheel = 0.0;
    m_lfo.reset();
    std::size_t i = 0;
    for (const auto& f : fields()) f.put(m_s, m_params[i++]->get());
}

int Synth::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : m_voices) n += v.active() ? 1 : 0;
    return n;
}

void Synth::pushKey(int key) noexcept
{
    removeKey(key);
    if (m_stackSize == StackSize) {                 // forget the oldest
        std::move(m_stack.begin() + 1, m_stack.end(), m_stack.begin());
        --m_stackSize;
    }
    m_stack[std::size_t(m_stackSize++)] = key;
}

void Synth::removeKey(int key) noexcept
{
    const auto end = m_stack.begin() + m_stackSize;
    const auto it = std::remove(m_stack.begin(), end, key);
    m_stackSize = int(it - m_stack.begin());
}

// A free voice; past the polyphony the oldest held key fades; with the pool full, the quietest goes.
synth::Voice* Synth::freeVoice() noexcept
{
    int held = 0;
    Voice* oldest = nullptr;
    std::uint64_t oldestStart = 0;
    for (std::size_t i = 0; i < m_voices.size(); ++i) {
        auto& v = m_voices[i];
        if (!v.active() || v.releasing()) continue;
        ++held;
        if (!oldest || m_started[i] < oldestStart) { oldest = &v; oldestStart = m_started[i]; }
    }
    if (held >= Polyphony && oldest) oldest->fadeOut(m_sampleRate);

    Voice* quietest = nullptr;
    for (auto& v : m_voices) {
        if (!v.active()) return &v;
        if (!quietest || v.level() < quietest->level()) quietest = &v;
    }
    if (quietest) quietest->stop();
    return quietest;
}

void Synth::noteOn(int key, int velocity) noexcept
{
    m_down[std::size_t(key)] = true;
    m_velocity[std::size_t(key)] = velocity;
    if (monophonic()) {
        auto& v = m_voices[0];
        const bool overlap = m_stackSize > 0 && v.active() && !v.releasing();
        pushKey(key);
        const bool legato = m_s.mode == PlayMode::Legato;
        const bool retrigger = !legato || !overlap;
        const double glide = (!legato || overlap) ? m_s.glideSeconds : 0.0;
        v.start(key, velocity, retrigger, glide, m_sampleRate);
        m_monoSustained = false;
        return;
    }

    Voice* voice = nullptr;
    for (auto& v : m_voices)                        // the key's own voice, if it still sounds
        if (v.active() && v.key() == key) { voice = &v; break; }
    if (!voice) voice = freeVoice();
    if (!voice) return;
    voice->start(key, velocity, true, 0.0, m_sampleRate);
    const auto index = std::size_t(voice - m_voices.data());
    m_started[index] = ++m_clock;
    m_sustained[index] = false;
}

void Synth::noteOff(int key) noexcept
{
    m_down[std::size_t(key)] = false;
    if (monophonic()) {
        auto& v = m_voices[0];
        removeKey(key);
        if (v.key() != key || !v.active() || v.releasing()) return;   // a key no longer sounding
        if (m_stackSize > 0) {                      // back to the newest key still held
            const int back = m_stack[std::size_t(m_stackSize - 1)];
            const bool legato = m_s.mode == PlayMode::Legato;
            v.start(back, m_velocity[std::size_t(back)], !legato, m_s.glideSeconds, m_sampleRate);
        } else if (m_pedal) {
            m_monoSustained = true;
        } else {
            v.release();
        }
        return;
    }
    for (std::size_t i = 0; i < m_voices.size(); ++i) {
        auto& v = m_voices[i];
        if (!v.active() || v.releasing() || v.key() != key) continue;
        if (m_pedal) m_sustained[i] = true;
        else v.release();
    }
}

void Synth::pedal(bool down) noexcept
{
    m_pedal = down;
    if (down) return;
    if (monophonic()) {
        auto& v = m_voices[0];
        if (m_monoSustained && m_stackSize == 0 && v.active()) v.release();
        m_monoSustained = false;
        return;
    }
    for (std::size_t i = 0; i < m_voices.size(); ++i) {
        auto& v = m_voices[i];
        if (m_sustained[i] && v.active() && !m_down[std::size_t(std::clamp(v.key(), 0, 127))]) v.release();
        m_sustained[i] = false;
    }
}

void Synth::allNotesOff() noexcept
{
    for (auto& v : m_voices) v.release();
    m_sustained.fill(false);
    m_down.fill(false);
    m_stackSize = 0;
    m_monoSustained = false;
}

void Synth::allSoundOff() noexcept
{
    for (auto& v : m_voices) v.stop();
    allNotesOff();
}

void Synth::handle(const MidiEvent& e) noexcept
{
    const int type = e.status & 0xF0;
    const int key = e.data1 & 0x7F;
    if (type == 0x90 && e.data2 > 0) noteOn(key, e.data2 & 0x7F);
    else if (type == 0x80 || type == 0x90) noteOff(key);
    else if (type == 0xE0) {
        const int value = ((e.data2 & 0x7F) << 7) | (e.data1 & 0x7F);
        m_bend = std::clamp((value - 8192) / 8192.0, -1.0, 1.0);
    } else if (type == 0xB0) {
        if (e.data1 == 1) m_modWheel = (e.data2 & 0x7F) / 127.0;
        else if (e.data1 == 64) pedal(e.data2 >= 64);
        else if (e.data1 == 123) allNotesOff();
        else if (e.data1 == 120) allSoundOff();
    }
}

void Synth::renderBlock(float* mono, int frames) noexcept
{
    readSettings();

    // The LFO: locked to the song while a synced one plays, else running at its rate.
    const auto& l = m_s.lfo;
    const double hz = lfoRateHz(l, m_bpm);
    if (l.sync && m_playing && m_bpm > 0.0) {
        const double beat = m_beat + (m_bufferFrame / m_sampleRate) * m_bpm / 60.0;
        m_lfo.setPhase(beat * hz * 60.0 / m_bpm);
    }
    const double value = m_lfo.value(l.shape);
    const double depth = std::clamp(l.depth + m_modWheel, 0.0, 1.0);

    VoiceControls c;
    c.settings = &m_s;
    c.sampleRate = m_sampleRate;
    c.bendSemitones = m_bend * m_s.bendSemitones;
    switch (l.target) {
    case LfoTarget::Pitch: c.lfoSemitones = LfoPitchSemitones * depth * depth * value; break;
    case LfoTarget::Filter: c.lfoOctaves = LfoFilterOctaves * depth * value; break;
    case LfoTarget::Volume: c.lfoVolumeDip = depth * (0.5 - 0.5 * value); break;
    case LfoTarget::PulseWidth: c.lfoPulseWidth = LfoPulseWidth * depth * value; break;
    }

    for (auto& v : m_voices) v.render(mono, frames, c);
    if (!(l.sync && m_playing)) m_lfo.advance(hz * frames / m_sampleRate);

    const float gain = 0.5f * std::pow(10.0f, float(m_s.volumeDb) / 20.0f);
    for (int i = 0; i < frames; ++i) mono[i] *= gain;
}

void Synth::renderRange(const AudioView& audio, int start, int end) noexcept
{
    while (start < end) {
        const int frames = std::min(Block, end - start);
        std::array<float, Block> mono{};
        m_bufferFrame = start;
        renderBlock(mono.data(), frames);
        if (audio.channelCount == 1) {
            for (int i = 0; i < frames; ++i) audio.channel(0)[start + i] += mono[std::size_t(i)];
        } else {
            for (int i = 0; i < frames; ++i) {
                audio.channel(0)[start + i] += mono[std::size_t(i)];
                audio.channel(1)[start + i] += mono[std::size_t(i)];
            }
        }
        start += frames;
    }
}

void Synth::process(ProcessContext& context) noexcept
{
    const auto& audio = context.audio;
    if (audio.frames <= 0 || audio.channelCount <= 0 || !audio.channels) return;
    m_bpm = context.bpm > 0.0 ? context.bpm : 120.0;
    m_beat = context.beat;
    m_playing = context.playing;
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
