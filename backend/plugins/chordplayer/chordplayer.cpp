#include "plugins/chordplayer/chordplayer.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double EntryGuard = 0.12;       // beats after a press in which grid steps are skipped (no flam)
constexpr double Forever = 1e12;

bool isNoteOn(const MidiEvent& e) { return (e.status & 0xF0) == 0x90 && e.data2 > 0; }
bool isNoteOff(const MidiEvent& e) { return (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0); }
bool isAllOff(const MidiEvent& e) { return (e.status & 0xF0) == 0xB0 && (e.data1 == 123 || e.data1 == 120); }

} // namespace

ChordPlayer::Strip ChordPlayer::stripOf(const chords::Chord& chord)
{
    Strip s;
    const auto tones = chords::chordTones(chord);
    s.count = int(std::min<std::size_t>(tones.size(), 4));
    for (int i = 0; i < s.count; ++i) s.tones[std::size_t(i)] = tones[std::size_t(i)];
    s.bass = chord.bassClass();
    return s;
}

ChordPlayer::ChordPlayer()
{
    for (auto& v : m_shared) v.store(0, std::memory_order_relaxed);
}

int ChordPlayer::stripOfKey(int key, bool& bass) noexcept
{
    for (int i = 0; i < Strips; ++i) {
        if (ChordKeys[std::size_t(i)] == key) { bass = false; return i; }
        if (BassKeys[std::size_t(i)] == key) { bass = true; return i; }
    }
    return -1;
}

void ChordPlayer::setStrips(const std::array<Strip, Strips>& strips) noexcept
{
    for (int i = 0; i < Strips; ++i) {
        const auto& s = strips[std::size_t(i)];
        const std::size_t base = std::size_t(i) * 6;
        m_shared[base].store(std::clamp(s.count, 0, 4), std::memory_order_relaxed);
        m_shared[base + 1].store(s.bass, std::memory_order_relaxed);
        for (std::size_t t = 0; t < 4; ++t) m_shared[base + 2 + t].store(s.tones[t], std::memory_order_relaxed);
    }
    m_generation.fetch_add(1, std::memory_order_release);
}

void ChordPlayer::setAutoplay(int mode) noexcept
{
    m_autoplay.store(std::clamp(mode, 0, chords::AutoplayModes - 1), std::memory_order_relaxed);
}

void ChordPlayer::prepare(double sampleRate) noexcept
{
    m_sampleRate = sampleRate > 1000 ? sampleRate : 48000.0;
    reset();
}

void ChordPlayer::reset() noexcept
{
    for (auto& s : m_sounding) s = Sounding{};
    m_strip = -1;
    m_held = false;
    m_wasPlaying = false;
    m_freeBeat = 0;
    m_gridOrigin = 0;
    m_entryBeat = -1e9;
    m_outCount = 0;
    publishActive();
}

void ChordPlayer::pullSettings() noexcept
{
    const auto generation = m_generation.load(std::memory_order_acquire);
    if (generation == m_seenGeneration) return;
    m_seenGeneration = generation;
    for (int i = 0; i < Strips; ++i) {
        const std::size_t base = std::size_t(i) * 6;
        auto& s = m_strips[std::size_t(i)];
        s.count = m_shared[base].load(std::memory_order_relaxed);
        s.bass = m_shared[base + 1].load(std::memory_order_relaxed);
        for (std::size_t t = 0; t < 4; ++t) s.tones[t] = m_shared[base + 2 + t].load(std::memory_order_relaxed);
    }
    // The strip playing now changed: follow it (or stop if it was emptied).
    if (m_strip >= 0) {
        const auto& s = m_strips[std::size_t(m_strip)];
        if (s.count == 0) { stopAll(0); m_strip = -1; m_held = false; return; }
        m_voicing = chords::voiceTones(std::span<const int>(s.tones.data(), std::size_t(s.count)), m_voicing);
        m_bassNote = chords::bassNote(s.bass, m_bassNote);
    }
}

void ChordPlayer::rebuildPattern(int numerator, int denominator) noexcept
{
    const int mode = m_autoplay.load(std::memory_order_relaxed);
    if (mode == m_patternMode && numerator == m_patternNumerator && denominator == m_patternDenominator && m_bass == m_patternBass)
        return;
    m_patternMode = mode;
    m_patternNumerator = numerator;
    m_patternDenominator = denominator;
    m_patternBass = m_bass;
    chords::buildPattern(mode, numerator, denominator, m_bass ? chords::Part::Bass : chords::Part::Chord, m_pattern);
}

int ChordPlayer::offsetAt(double beat) const noexcept
{
    if (m_beatsPerSample <= 0 || m_frames <= 0) return 0;
    const double offset = std::round((beat - m_blockStart) / m_beatsPerSample);
    return int(std::clamp(offset, 0.0, double(m_frames - 1)));
}

void ChordPlayer::send(int offset, std::uint8_t status, int key, int velocity) noexcept
{
    if (m_outCount >= MaxEvents || key < 0 || key > 127) return;
    m_out[std::size_t(m_outCount++)] = MidiEvent{offset, status, std::uint8_t(key), std::uint8_t(std::clamp(velocity, 0, 127))};
}

void ChordPlayer::noteOn(int key, int velocity, double beat, double lengthBeats) noexcept
{
    if (key < 0 || key > 127) return;
    const int offset = offsetAt(beat);
    for (auto& s : m_sounding)
        if (s.key == key) { send(offset, 0x80, key, 0); s.key = -1; }   // struck again: end it first
    for (auto& s : m_sounding) {
        if (s.key >= 0) continue;
        s.key = key;
        s.offBeat = beat + lengthBeats;
        send(offset, 0x90, key, std::clamp(velocity, 1, 127));
        return;
    }
}

void ChordPlayer::stopAll(int offset) noexcept
{
    for (auto& s : m_sounding) {
        if (s.key < 0) continue;
        send(offset, 0x80, s.key, 0);
        s.key = -1;
    }
}

void ChordPlayer::releaseDue(double fromBeat, double toBeat) noexcept
{
    for (auto& s : m_sounding) {
        if (s.key < 0 || s.offBeat >= toBeat) continue;
        send(offsetAt(std::max(s.offBeat, fromBeat)), 0x80, s.key, 0);
        s.key = -1;
    }
}

int ChordPlayer::keyFor(int voice) const noexcept
{
    if (voice == chords::VoiceBass) return m_bassNote;
    if (voice == chords::VoiceBassOctave) return m_bassNote >= 0 ? m_bassNote + 12 : -1;
    if (voice < 0 || m_voicing.count <= 0) return -1;
    return m_voicing.notes[std::size_t(voice % m_voicing.count)] + 12 * (voice / m_voicing.count);
}

void ChordPlayer::playSteps(double fromBeat, double toBeat) noexcept
{
    if (m_strip < 0 || m_held || toBeat <= fromBeat || m_pattern.count == 0 || m_pattern.barBeats <= 0) return;
    const double bar = m_pattern.barBeats;
    const double scale = m_velocity / 100.0;
    for (double k = std::floor((fromBeat - m_gridOrigin) / bar); ; k += 1) {
        const double barStart = m_gridOrigin + k * bar;
        if (barStart >= toBeat) break;
        for (int i = 0; i < m_pattern.count; ++i) {
            const auto& step = m_pattern.steps[std::size_t(i)];
            const double at = barStart + step.beat;
            if (at < fromBeat || at >= toBeat) continue;
            if (at >= m_entryBeat - 1e-9 && at < m_entryBeat + EntryGuard) continue;   // the press struck already
            const int velocity = int(std::lround(step.velocity * scale));
            if (step.voice == chords::VoiceChord) {
                for (int n = 0; n < m_voicing.count; ++n) noteOn(m_voicing.notes[std::size_t(n)], velocity, at, step.length);
            } else {
                noteOn(keyFor(step.voice), velocity, at, step.length);
            }
        }
    }
}

void ChordPlayer::press(int strip, bool bass, int velocity, double beat, int offset) noexcept
{
    const auto& s = m_strips[std::size_t(strip)];
    if (s.count == 0) return;                                          // an empty strip
    const int mode = m_autoplay.load(std::memory_order_relaxed);
    const bool latched = m_strip >= 0 && !m_held;
    if (mode != chords::AutoplayOff && latched && m_strip == strip && m_bass == bass) {
        stopAll(offset);                                               // the same strip again: stop
        m_strip = -1;
        return;
    }
    stopAll(offset);
    const bool fresh = m_strip < 0;
    m_strip = strip;
    m_bass = bass;
    m_held = mode == chords::AutoplayOff;
    m_velocity = std::clamp(velocity, 1, 127);
    m_entryBeat = beat;
    m_voicing = chords::voiceTones(std::span<const int>(s.tones.data(), std::size_t(s.count)), m_voicing);
    m_bassNote = chords::bassNote(s.bass, m_bassNote);
    if (fresh && !m_wasPlaying) m_gridOrigin = beat;                   // stopped: bar one starts at the press

    const double length = m_held ? Forever : 0.95;
    const double scale = m_velocity / 100.0;
    if (!bass)
        for (int n = 0; n < m_voicing.count; ++n)
            noteOn(m_voicing.notes[std::size_t(n)], int(std::lround(92 * scale)), beat, length);
    noteOn(m_bassNote, int(std::lround(100 * scale)), beat, length);
}

void ChordPlayer::releaseTrigger(int strip, bool bass, int offset) noexcept
{
    if (!m_held || m_strip != strip || m_bass != bass) return;         // autoplay keeps going
    stopAll(offset);
    m_strip = -1;
    m_held = false;
}

void ChordPlayer::publishActive() noexcept
{
    const int mode = m_autoplay.load(std::memory_order_relaxed);
    m_activeStrip.store(m_strip, std::memory_order_relaxed);
    m_activeBass.store(m_strip >= 0 && m_bass, std::memory_order_relaxed);
    m_running.store(m_strip >= 0 && !m_held && mode != chords::AutoplayOff, std::memory_order_relaxed);
}

void ChordPlayer::process(ProcessContext& context) noexcept
{
    m_outCount = 0;
    if (!context.midi) return;
    pullSettings();
    const int mode = m_autoplay.load(std::memory_order_relaxed);
    const int numerator = context.timeSigNumerator > 0 ? context.timeSigNumerator : 4;
    const int denominator = context.timeSigDenominator > 0 ? context.timeSigDenominator : 4;
    const double bpm = context.bpm > 1 ? context.bpm : 120.0;
    const double rate = context.sampleRate > 1000 ? context.sampleRate : m_sampleRate;
    m_frames = std::max(context.audio.frames, 1);
    m_beatsPerSample = bpm / 60.0 / rate;

    // The clock: the song's beats while playing, a free one from the press while stopped. A start,
    // stop or jump (seek, loop) ends what plays. A block covers the beats that round to its own
    // samples: from half a sample before its first to half a sample before its end, so a note is
    // never a sample early and never played twice.
    const double half = 0.5 * m_beatsPerSample;
    double start;
    if (context.playing) {
        const bool jump = !m_wasPlaying || std::abs(context.beat - half - m_expectedBeat) > half + 1e-9;
        m_blockStart = context.beat;
        if (jump) { stopAll(0); m_strip = -1; m_held = false; m_gridOrigin = 0; start = context.beat - half; }
        else start = m_expectedBeat;
    } else {
        m_blockStart = m_freeBeat;
        if (m_wasPlaying) { stopAll(0); m_strip = -1; m_held = false; }
        start = m_freeBeat - half;
    }
    const double end = m_blockStart + m_frames * m_beatsPerSample - half;
    m_wasPlaying = context.playing;
    if (mode == chords::AutoplayOff && m_strip >= 0 && !m_held) { stopAll(0); m_strip = -1; }   // autoplay turned off
    rebuildPattern(numerator, denominator);

    // Walk the incoming events in time, playing the pattern up to each one.
    const auto events = context.midi->events();
    const int inCount = int(std::min<std::size_t>(events.size(), MaxEvents));
    std::copy_n(events.begin(), inCount, m_in.begin());
    context.midi->clear();
    double cursor = start;
    for (int i = 0; i < inCount; ++i) {
        const MidiEvent& e = m_in[std::size_t(i)];
        const double at = std::max(cursor, m_blockStart + e.offset * m_beatsPerSample);
        releaseDue(cursor, at);
        playSteps(cursor, at);
        cursor = at;
        bool bass = false;
        const int strip = (isNoteOn(e) || isNoteOff(e)) ? stripOfKey(e.data1, bass) : -1;
        if (strip >= 0 && isNoteOn(e)) {
            press(strip, bass, e.data2, at, e.offset);
            rebuildPattern(numerator, denominator);                  // the area may have changed
        } else if (strip >= 0) {
            releaseTrigger(strip, bass, e.offset);
        } else {
            if (isAllOff(e)) { stopAll(e.offset); m_strip = -1; m_held = false; }
            if (m_outCount < MaxEvents) m_out[std::size_t(m_outCount++)] = e;   // not ours: to the piano
        }
    }
    releaseDue(cursor, end);
    playSteps(cursor, end);

    for (int i = 0; i < m_outCount; ++i) context.midi->add(m_out[std::size_t(i)]);
    context.midi->sort();
    if (context.playing) m_expectedBeat = end;
    else m_freeBeat = m_blockStart + m_frames * m_beatsPerSample;
    publishActive();
}
