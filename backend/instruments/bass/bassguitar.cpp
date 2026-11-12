#include "instruments/bass/bassguitar.h"
#include "instruments/bass/bassfretboard.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

using namespace bass;

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

#define BASS_FIELD(ID, NAME, MIN, MAX, EXPR)                                                   \
    Field{ID, NAME, double(MIN), double(MAX),                                                  \
          [](const Settings& s) { return toDouble(s.EXPR); },                                  \
          [](Settings& s, double v) { assign(s.EXPR, v); }}

const auto& fields()
{
    static const Field table[] = {
        BASS_FIELD("style", "Style", 0, 2, style),
        BASS_FIELD("muteAmount", "Palm mute", 0, 1, muteAmount),
        BASS_FIELD("slapOnHardHits", "Slap on hard hits", 0, 1, slapOnHardHits),
        BASS_FIELD("slapVelocity", "Slap velocity", 1, 127, slapVelocity),
        BASS_FIELD("keyswitches", "Keyswitches", 0, 1, keyswitches),
        BASS_FIELD("pluckPosition", "Pluck position", 0, 1, pluckPosition),
        BASS_FIELD("sustain", "Sustain", 0, 1, sustain),
        BASS_FIELD("brightness", "Brightness", 0, 1, brightness),
        BASS_FIELD("velocity", "Velocity", 0, 1, velocity),
        BASS_FIELD("pickupBlend", "Pickup blend", 0, 1, pickupBlend),
        BASS_FIELD("tone", "Tone", 0, 1, tone),
        BASS_FIELD("drive", "Drive", 0, 1, drive),
        BASS_FIELD("compression", "Compression", 0, 1, compression),
        BASS_FIELD("slide", "Slide", 0, MaxSlideSeconds, slideSeconds),
        BASS_FIELD("bendRange", "Bend range", 0, MaxBendSemitones, bendSemitones),
        BASS_FIELD("volume", "Volume", MinVolumeDb, MaxVolumeDb, volumeDb),
    };
    return table;
}

#undef BASS_FIELD

// Settings::sustain and brightness as decay times (T60, seconds), on log scales.
double ringSeconds(double sustain) noexcept { return 1.5 * std::pow(12.0 / 1.5, sustain); }
double overtoneSeconds(double brightness) noexcept { return 0.06 * std::pow(1.2 / 0.06, brightness); }

} // namespace

BassGuitar::BassGuitar()
{
    const Settings defaults;
    for (const auto& f : fields())
        m_params.push_back(&addParameter(f.id, f.name, float(f.min), float(f.max), float(f.get(defaults))));
    prepare(48000.0, 1024);
}

void BassGuitar::setSettings(const Settings& input) noexcept
{
    const auto s = sanitize(input);
    std::size_t i = 0;
    for (const auto& f : fields()) m_params[i++]->set(float(f.get(s)));
}

Settings BassGuitar::settings() const noexcept
{
    Settings s;
    std::size_t i = 0;
    for (const auto& f : fields()) f.put(s, m_params[i++]->get());
    return s;
}

void BassGuitar::readSettings() noexcept
{
    std::size_t i = 0;
    for (const auto& f : fields()) f.put(m_s, m_params[i++]->get());
    // A new style on the page wins over the last keyswitch.
    if (m_s.style != m_pageStyle) { m_pageStyle = m_s.style; m_switched = -1; }
    if (!m_s.keyswitches) m_switched = -1;
    m_tone.set(m_s.tone, m_s.drive, m_s.compression);
}

void BassGuitar::prepare(double sampleRate, int maxFrames)
{
    (void)maxFrames;                                // works in its own short blocks
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& s : m_strings)
        for (auto& v : s.voices) v.prepare(m_sampleRate);
    m_tone.prepare(m_sampleRate);
    reset();
}

void BassGuitar::reset() noexcept
{
    for (auto& s : m_strings) {
        for (auto& v : s.voices) v.stop();
        s.current = 0;
        s.key = -1;
        s.since = 0;
        s.sinceFrame = 0;
        s.sustained = false;
    }
    m_keyString.fill(-1);
    m_clock = 0;
    m_frame = 0;
    m_handFret = 5.0;
    m_pedal = false;
    m_bend = 0.0;
    std::size_t i = 0;
    for (const auto& f : fields()) f.put(m_s, m_params[i++]->get());
    m_pageStyle = m_s.style;
    m_switched = -1;
    m_tone.reset();
    m_tone.set(m_s.tone, m_s.drive, m_s.compression);
}

int BassGuitar::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& s : m_strings)
        for (const auto& v : s.voices) n += v.active() ? 1 : 0;
    return n;
}

int BassGuitar::stringOf(int key) const noexcept
{
    return key >= 0 && key < 128 ? m_keyString[std::size_t(key)] : -1;
}

Style BassGuitar::currentStyle() const noexcept
{
    return m_switched >= 0 ? Style(m_switched) : m_s.style;
}

StringControls BassGuitar::stringControls() const noexcept
{
    StringControls c;
    c.sampleRate = m_sampleRate;
    c.bendSemitones = m_bend * m_s.bendSemitones;
    c.ringSeconds = ringSeconds(m_s.sustain);
    c.overtoneSeconds = overtoneSeconds(m_s.brightness);
    c.muteAmount = m_s.muteAmount;
    c.pickupBlend = m_s.pickupBlend;
    return c;
}

// A key over one held for a moment slides there on that key's string, when the string reaches.
bool BassGuitar::trySlide(int key, int played) noexcept
{
    if (m_s.slideSeconds <= 0.0) return false;
    const auto after = std::int64_t(SlideAfterSeconds * m_sampleRate);
    int from = -1;
    for (int s = 0; s < StringCount; ++s) {
        const auto& st = m_strings[std::size_t(s)];
        if (st.key < 0) continue;
        if (from < 0 || st.since > m_strings[std::size_t(from)].since) from = s;   // the newest held
    }
    if (from < 0) return false;
    auto& st = m_strings[std::size_t(from)];
    auto& voice = st.voices[std::size_t(st.current)];
    if (m_frame - st.sinceFrame < after || !voice.active() || voice.releasing()) return false;
    const int fret = fretOn(from, played);
    if (fret < 0 || std::abs(played - st.key) > 12) return false;

    voice.slideTo(played, m_s.slideSeconds, m_sampleRate);
    m_keyString[std::size_t(st.key)] = -1;          // the old key now plays nothing
    st.key = key;
    st.since = ++m_clock;
    st.sinceFrame = m_frame;
    st.sustained = false;
    m_keyString[std::size_t(key)] = from;
    if (fret > 0) m_handFret = fret;
    return true;
}

void BassGuitar::noteOn(int key, int velocity) noexcept
{
    Style switched{};
    if (m_s.keyswitches && keyswitchStyle(key, switched)) {
        m_switched = int(switched);
        return;
    }
    if (m_keyString[std::size_t(key)] >= 0) noteOff(key);   // the same key again: start afresh
    const int played = foldIntoRange(key);
    if (trySlide(key, played)) return;

    std::array<bool, StringCount> held{};
    std::array<unsigned long long, StringCount> since{};
    for (int s = 0; s < StringCount; ++s) {
        held[std::size_t(s)] = m_strings[std::size_t(s)].key >= 0;
        since[std::size_t(s)] = m_strings[std::size_t(s)].since;
    }
    const auto at = choose(played, held, since, m_handFret);
    auto& st = m_strings[std::size_t(at.string)];
    if (st.key >= 0) m_keyString[std::size_t(st.key)] = -1;  // a held key loses its string

    // The note's kind from the style.
    const double v = std::clamp(velocity / 127.0, 0.0, 1.0);
    Style style = currentStyle();
    if (style == Style::Finger && m_s.slapOnHardHits && velocity >= m_s.slapVelocity) style = Style::Slap;
    NoteKind kind = NoteKind::Finger;
    if (style == Style::Slap) kind = at.string <= 1 ? NoteKind::Slap : NoteKind::Pop;
    else if (style == Style::Mute) kind = NoteKind::Muted;

    const double sens = m_s.velocity;
    const double strength = 0.85 * std::pow(std::max(v, 0.01), 2.2 * sens);   // sens 0: every note the same
    const double brightness = (1.0 - sens) * 0.6 + sens * v;
    const double strikeAt = 0.07 + 0.23 * m_s.pluckPosition;

    // One note per string: the one ringing fades, the other voice takes the new note.
    auto& old = st.voices[std::size_t(st.current)];
    if (old.active()) old.fadeOut(m_sampleRate);
    st.current = 1 - st.current;
    auto& voice = st.voices[std::size_t(st.current)];
    voice.stop();
    voice.start(OpenKeys[at.string], played, kind, strength, brightness, strikeAt, stringControls());

    st.key = key;
    st.since = ++m_clock;
    st.sinceFrame = m_frame;
    st.sustained = false;
    m_keyString[std::size_t(key)] = at.string;
    if (at.fret > 0) m_handFret = at.fret;
}

void BassGuitar::noteOff(int key) noexcept
{
    const int s = m_keyString[std::size_t(key)];
    if (s < 0) return;
    m_keyString[std::size_t(key)] = -1;
    auto& st = m_strings[std::size_t(s)];
    if (st.key != key) return;
    st.key = -1;
    if (m_pedal) st.sustained = true;
    else st.voices[std::size_t(st.current)].release();
}

void BassGuitar::pedal(bool down) noexcept
{
    m_pedal = down;
    if (down) return;
    for (auto& st : m_strings) {
        if (st.sustained && st.key < 0) st.voices[std::size_t(st.current)].release();
        st.sustained = false;
    }
}

void BassGuitar::allNotesOff() noexcept
{
    for (auto& st : m_strings) {
        for (auto& v : st.voices) v.release();
        st.key = -1;
        st.sustained = false;
    }
    m_keyString.fill(-1);
}

void BassGuitar::allSoundOff() noexcept
{
    for (auto& st : m_strings)
        for (auto& v : st.voices) v.stop();
    allNotesOff();
}

void BassGuitar::handle(const MidiEvent& e) noexcept
{
    const int type = e.status & 0xF0;
    const int key = e.data1 & 0x7F;
    if (type == 0x90 && e.data2 > 0) noteOn(key, e.data2 & 0x7F);
    else if (type == 0x80 || type == 0x90) noteOff(key);
    else if (type == 0xE0) {
        const int value = ((e.data2 & 0x7F) << 7) | (e.data1 & 0x7F);
        m_bend = std::clamp((value - 8192) / 8192.0, -1.0, 1.0);
    } else if (type == 0xB0) {
        if (e.data1 == 64) pedal(e.data2 >= 64);
        else if (e.data1 == 123) allNotesOff();
        else if (e.data1 == 120) allSoundOff();
    }
}

void BassGuitar::renderBlock(float* mono, int frames) noexcept
{
    readSettings();
    const auto c = stringControls();
    for (auto& st : m_strings)
        for (auto& v : st.voices) v.render(mono, frames, c);
    const float gain = 0.3f * std::pow(10.0f, float(m_s.volumeDb) / 20.0f);
    for (int i = 0; i < frames; ++i) mono[i] = m_tone.process(mono[i]) * gain;
    m_frame += frames;
}

void BassGuitar::renderRange(const AudioView& audio, int start, int end) noexcept
{
    while (start < end) {
        const int frames = std::min(Block, end - start);
        std::array<float, Block> mono{};
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

void BassGuitar::process(ProcessContext& context) noexcept
{
    const auto& audio = context.audio;
    if (audio.frames <= 0 || audio.channelCount <= 0 || !audio.channels) return;
    int frame = 0;
    if (context.midi) {
        for (const auto& event : context.midi->events()) {
            const int at = std::clamp(event.offset, 0, audio.frames);
            if (at > frame) { renderRange(audio, frame, at); frame = at; }
            readSettings();                         // keyswitch and style rules see the latest page
            handle(event);
        }
    }
    renderRange(audio, frame, audio.frames);
}
