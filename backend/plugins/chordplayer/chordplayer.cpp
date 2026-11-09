#include "plugins/chordplayer/chordplayer.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double EntryGuard = 0.12;       // beats around a press in which it joins a grid step
constexpr double PedalLagMs = 30.0;       // the pedal goes down this long after a press
constexpr int SustainPedal = 64;
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

int ChordPlayer::stripOfKey(int key, bool& bass, int& zone) noexcept
{
    zone = -1;
    for (int i = 0; i < Strips; ++i) {
        const auto k = std::size_t(i);
        if (ChordKeys[k] == key) { bass = false; return i; }
        if (BassKeys[k] == key) { bass = true; return i; }
        if (LowChordKeys[k] == key) { bass = false; zone = Low; return i; }
        if (HighChordKeys[k] == key) { bass = false; zone = High; return i; }
    }
    return -1;
}

int ChordPlayer::stripOfKey(int key, bool& bass) noexcept
{
    int zone = -1;
    return stripOfKey(key, bass, zone);
}

int ChordPlayer::zoneOfKey(int key) noexcept
{
    for (int z = 0; z < 3; ++z) if (ZoneKeys[std::size_t(z)] == key) return z;
    return -1;
}

ChordPlayer::Trigger ChordPlayer::triggerNotes(int strip, bool bass, int zone, KeyMapping mapping) noexcept
{
    Trigger t;
    if (strip < 0 || strip >= Strips) return t;
    const auto k = std::size_t(strip);
    zone = std::clamp(zone, 0, 2);
    if (bass) t.stripKey = BassKeys[k];
    else if (mapping == KeyMapping::ZoneKeys) { t.keys[std::size_t(t.count++)] = ZoneKeys[std::size_t(zone)]; t.stripKey = ChordKeys[k]; }
    else t.stripKey = zone == Low ? LowChordKeys[k] : zone == High ? HighChordKeys[k] : ChordKeys[k];
    t.keys[std::size_t(t.count++)] = t.stripKey;
    return t;
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

void ChordPlayer::setVariation(int variation) noexcept
{
    m_variation.store(std::clamp(variation, 0, chords::Variations - 1), std::memory_order_relaxed);
}

void ChordPlayer::setDynamics(int dynamics) noexcept
{
    m_dynamics.store(std::clamp(dynamics, 0, chords::DynamicsModes - 1), std::memory_order_relaxed);
}

void ChordPlayer::setBassWithChords(bool on) noexcept
{
    m_bassWithChords.store(on, std::memory_order_relaxed);
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
    m_zone = m_zoneSelect = Middle;
    m_triggerKey = -1;
    m_wasPlaying = false;
    m_freeBeat = 0;
    m_gridOrigin = 0;
    m_entryGrid = -1e9;
    m_entryShift = 0;
    m_pedalDown = m_pedalPending = false;
    m_changePending = false;
    m_rootPending = false;
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
        placeChord();
    }
}

void ChordPlayer::rebuildPattern(int numerator, int denominator) noexcept
{
    const int mode = m_autoplay.load(std::memory_order_relaxed);
    const chords::Part part = m_bass ? chords::Part::Bass
                            : m_bassWithChords.load(std::memory_order_relaxed) ? chords::Part::Chord : chords::Part::RightHand;
    const int variation = m_variation.load(std::memory_order_relaxed);
    if (mode == m_patternMode && variation == m_patternVariation && numerator == m_patternNumerator
        && denominator == m_patternDenominator && part == m_patternPart)
        return;
    m_patternMode = mode;
    m_patternVariation = variation;
    m_patternNumerator = numerator;
    m_patternDenominator = denominator;
    m_patternPart = part;
    chords::buildPattern(mode, variation, numerator, denominator, part, m_pattern);
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
    if (m_pedalDown) send(offset, 0xB0, SustainPedal, 0);
    m_pedalDown = m_pedalPending = false;
    m_changePending = false;
    m_rootPending = false;
}

int ChordPlayer::velocityOf(bool top, int accent) const noexcept
{
    const int dynamics = m_dynamics.load(std::memory_order_relaxed);
    const int base = dynamics == chords::DynamicsTopNote ? (top ? 104 : 80)
                   : dynamics == chords::DynamicsMeter ? accent
                   : 88;
    return std::clamp(int(std::lround(base * m_velocity / 100.0)), 1, 127);
}

void ChordPlayer::pedalDue(double fromBeat, double toBeat) noexcept
{
    if (!m_pedalPending || m_pedalAt >= toBeat) return;
    send(offsetAt(std::max(m_pedalAt, fromBeat)), 0xB0, SustainPedal, 127);
    m_pedalPending = false;
    m_pedalDown = true;
}

void ChordPlayer::placeChord() noexcept
{
    const auto& s = m_strips[std::size_t(m_strip)];
    const auto range = chords::zoneRange(m_zone, s.count > 0 ? s.tones[0] : 0);
    m_voicing = chords::voiceTones(std::span<const int>(s.tones.data(), std::size_t(s.count)), m_voicing, range);
    m_bassNote = s.bass >= 0 ? chords::ChordsBass.low + ((s.bass % 12) + 12) % 12 : -1;   // octave 2
    m_toneCount = std::clamp(s.count, 0, 4);
    for (int i = 0; i < m_toneCount; ++i) {
        m_toneClasses[std::size_t(i)] = ((s.tones[std::size_t(i)] % 12) + 12) % 12;
        int offset = ((m_toneClasses[std::size_t(i)] - m_toneClasses[0]) % 12 + 12) % 12;
        if (i > 0) while (offset <= m_toneOffsets[std::size_t(i - 1)]) offset += 12;   // stacked upwards
        m_toneOffsets[std::size_t(i)] = offset;
    }
    // Arpeggios and top lines count up from the zone's root (middle: the root in octave 4).
    m_rootNote = m_toneCount > 0 ? range.low : -1;
}

void ChordPlayer::releaseDue(double fromBeat, double toBeat) noexcept
{
    for (auto& s : m_sounding) {
        if (s.key < 0 || s.offBeat >= toBeat) continue;
        send(offsetAt(std::max(s.offBeat, fromBeat)), 0x80, s.key, 0);
        s.key = -1;
    }
}

int ChordPlayer::inBass(int key) const noexcept
{
    if (m_bassNote < 0) return key;
    const auto range = chords::bassRange(m_bassNote);
    while (key > range.high) key -= 12;
    while (key < range.low) key += 12;
    return key;
}

int ChordPlayer::keyFor(const chords::PatternStep& step) const noexcept
{
    const int degree = std::max(step.degree, 0);
    switch (step.kind) {
    case chords::Kind::Bass:
        return m_bassNote >= 0 ? inBass(m_bassNote + 12 * step.octave) : -1;
    case chords::Kind::Tone:
        if (m_rootNote < 0 || m_toneCount <= 0) return -1;
        return m_rootNote + m_toneOffsets[std::size_t(degree % m_toneCount)] + 12 * (degree / m_toneCount + step.octave);
    case chords::Kind::Low: {
        if (m_bassNote < 0 || m_toneCount <= 0) return -1;
        const int index = degree % m_toneCount;
        int above = ((m_toneClasses[std::size_t(index)] - m_bassNote) % 12 + 12) % 12;
        if (above == 0 && index != 0) above = 12;                      // a slash bass that is a chord tone
        return inBass(m_bassNote + above + 12 * (degree / m_toneCount + step.octave));
    }
    default:
        return -1;
    }
}

bool ChordPlayer::leftHandAt(double patternBeat) const noexcept
{
    for (int i = 0; i < m_pattern.count; ++i) {
        const auto& s = m_pattern.steps[std::size_t(i)];
        if ((s.kind == chords::Kind::Bass || s.kind == chords::Kind::Low) && std::abs(s.beat - patternBeat) < 1e-9) return true;
    }
    return false;
}

double ChordPlayer::untilLeftHand(double patternBeat) const noexcept
{
    const double loop = m_pattern.lengthBeats();
    double next = loop;
    for (int i = 0; i < m_pattern.count; ++i) {
        const auto& s = m_pattern.steps[std::size_t(i)];
        if (s.kind != chords::Kind::Bass && s.kind != chords::Kind::Low) continue;
        double ahead = std::fmod(s.beat - patternBeat + loop, loop);
        if (ahead < 1e-9) ahead = loop;
        next = std::min(next, ahead);
    }
    return next;
}

void ChordPlayer::playStep(const chords::PatternStep& step, double at, double fromBeat, double toBeat) noexcept
{
    const auto inside = [&](double beat) { return beat >= fromBeat && beat < toBeat; };
    const int top = m_voicing.count - 1;
    switch (step.kind) {
    case chords::Kind::Chord:
    case chords::Kind::Upper: {
        if (!inside(at)) return;
        const int first = step.kind == chords::Kind::Upper && m_voicing.count >= 3 ? 1 : 0;
        for (int n = first; n < m_voicing.count; ++n) noteOn(m_voicing.notes[std::size_t(n)], velocityOf(n == top, step.accent), at, step.length);
        return;
    }
    case chords::Kind::Roll:
        for (int n = 0; n < m_voicing.count; ++n) {
            const double delay = n * step.spreadMs * m_beatsPerMs;
            const double beat = at + delay;
            if (inside(beat)) noteOn(m_voicing.notes[std::size_t(n)], velocityOf(n == top, step.accent), beat, std::max(step.length - delay, 0.05));
        }
        return;
    case chords::Kind::Tone: {                                         // the top note's name sings out in a line
        if (!inside(at)) return;
        const int key = keyFor(step);
        noteOn(key, velocityOf(top >= 0 && key >= 0 && key % 12 == m_voicing.notes[std::size_t(top)] % 12, step.accent), at, step.length);
        return;
    }
    default:
        if (inside(at)) noteOn(keyFor(step), velocityOf(false, step.accent), at, step.length);
        return;
    }
}

void ChordPlayer::playSteps(double fromBeat, double toBeat) noexcept
{
    const double loop = m_pattern.lengthBeats();
    if (m_strip < 0 || m_held || toBeat <= fromBeat || m_pattern.count == 0 || loop <= 0) return;
    // From the loop before: a roll or a late sixteenth of it may still fall in this window.
    for (double k = std::floor((fromBeat - m_gridOrigin) / loop) - 1; ; k += 1) {
        const double loopStart = m_gridOrigin + k * loop;
        if (loopStart >= toBeat) break;
        for (int i = 0; i < m_pattern.count; ++i) {
            const auto& step = m_pattern.steps[std::size_t(i)];
            const double grid = loopStart + step.beat;
            double at = grid + step.nudgeMs * m_beatsPerMs;
            if (grid >= m_entryGrid - 1e-9 && grid < m_entryGrid + EntryGuard)
                at = std::max(at + m_entryShift, m_entryBeat);         // moved to the press, never before it
            if (at >= toBeat || at + 1.0 < fromBeat) continue;
            if (m_changePending && at >= fromBeat) {
                // A seamless change: the old chord rings until the pattern's next note, then ends.
                const bool root = m_rootPending;
                stopAll(offsetAt(at));
                m_rootPending = root;
                if (m_pattern.pedal) { m_pedalPending = true; m_pedalAt = at + PedalLagMs * m_beatsPerMs; }
            }
            if (m_rootPending && at >= fromBeat) {
                // The new chord's first bass-part note is its root, wherever in the bar it came in:
                // in place of the pattern's bass note there, or under the first right-hand note,
                // held to the pattern's next bass note.
                const bool leftHand = step.kind == chords::Kind::Bass || step.kind == chords::Kind::Low;
                if (leftHand) {
                    chords::PatternStep root = step;
                    root.kind = chords::Kind::Bass;
                    root.degree = 0;
                    root.octave = 0;
                    m_rootPending = false;
                    playStep(root, at, fromBeat, toBeat);
                    continue;
                }
                if (!leftHandAt(step.beat)) {
                    m_rootPending = false;
                    if (m_bassNote >= 0) noteOn(m_bassNote, velocityOf(false, step.accent), at, untilLeftHand(step.beat));
                }
            }
            playStep(step, at, fromBeat, toBeat);
        }
    }
}

void ChordPlayer::press(int strip, bool bass, int zone, int key, int velocity, double beat, int offset) noexcept
{
    const auto& s = m_strips[std::size_t(strip)];
    if (s.count == 0) return;                                          // an empty strip
    const int mode = m_autoplay.load(std::memory_order_relaxed);
    const bool latched = m_strip >= 0 && !m_held;
    zone = bass ? m_zone : std::clamp(zone, 0, 2);                     // the bass keeps the hand where it was
    if (mode != chords::AutoplayOff && latched && m_strip == strip && m_bass == bass && (bass || m_zone == zone)) {
        stopAll(offset);                                               // the same strip again: stop
        m_strip = -1;
        return;
    }
    // A change while a pattern plays is seamless: nothing struck, the grid goes on, and the new
    // chord comes in on the pattern's next note (see playSteps). Anything else starts afresh.
    const bool change = latched && mode != chords::AutoplayOff;
    if (!change) stopAll(offset);
    m_strip = strip;
    m_bass = bass;
    m_zone = zone;
    m_triggerKey = key;
    m_held = mode == chords::AutoplayOff;
    m_velocity = std::clamp(velocity, 1, 127);
    placeChord();
    rebuildPattern(m_numerator, m_denominator);                        // the area may have changed
    m_entryGrid = -1e9;
    m_entryShift = 0;
    m_entryBeat = beat;
    const double loop = m_pattern.lengthBeats();

    if (m_held) {                                                      // autoplay off: the chord while held
        const bool withBass = bass || m_bassWithChords.load(std::memory_order_relaxed);
        if (!bass)
            for (int n = 0; n < m_voicing.count; ++n)
                noteOn(m_voicing.notes[std::size_t(n)], velocityOf(n == m_voicing.count - 1), beat, Forever);
        if (withBass) noteOn(m_bassNote, velocityOf(false), beat, Forever);
        return;
    }
    if (m_pattern.count == 0 || loop <= 0) return;

    if (change) {
        m_changePending = true;
        m_rootPending = m_patternPart != chords::Part::RightHand;      // when the bass part plays
        // A press a hair late for a note of the pattern: that note again, now, with the new chord.
        const double k = std::floor((beat - m_gridOrigin) / loop);
        double best = Forever;
        for (double kk = k - 1; kk <= k + 1; kk += 1)
            for (int i = 0; i < m_pattern.count; ++i) {
                const double grid = m_gridOrigin + kk * loop + m_pattern.steps[std::size_t(i)].beat;
                if (grid > beat - EntryGuard && grid < beat - 1e-9 && grid < best) best = grid;
            }
        if (best < Forever) { m_entryGrid = best; m_entryShift = beat - best; }
        return;
    }

    // A first press. Stopped: the pattern starts at the press, from its first note. Playing: it
    // joins the song's grid at the step nearest the press (a little early or late), else at the
    // next one.
    if (!m_wasPlaying) m_gridOrigin = beat - m_pattern.steps[0].beat;
    const double k = std::floor((beat - m_gridOrigin) / loop);
    double best = Forever;
    for (double kk = k - 1; kk <= k + 1; kk += 1)
        for (int i = 0; i < m_pattern.count; ++i) {
            const double grid = m_gridOrigin + kk * loop + m_pattern.steps[std::size_t(i)].beat;
            if (grid > beat - EntryGuard && grid < beat + EntryGuard && grid < best) best = grid;
        }
    if (best < Forever) { m_entryGrid = best; m_entryShift = beat - best; }
    if (m_pattern.pedal) { m_pedalPending = true; m_pedalAt = beat + PedalLagMs * m_beatsPerMs; }
}

void ChordPlayer::releaseTrigger(int key, int offset) noexcept
{
    if (!m_held || m_strip < 0 || key != m_triggerKey) return;         // autoplay keeps going
    stopAll(offset);
    m_strip = -1;
    m_held = false;
}

void ChordPlayer::publishActive() noexcept
{
    const int mode = m_autoplay.load(std::memory_order_relaxed);
    m_activeStrip.store(m_strip, std::memory_order_relaxed);
    m_activeBass.store(m_strip >= 0 && m_bass, std::memory_order_relaxed);
    m_activeZone.store(m_zone, std::memory_order_relaxed);
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
    m_beatsPerMs = bpm / 60000.0;
    m_numerator = numerator;
    m_denominator = denominator;

    // The clock: the song's beats while playing, a free one from the press while stopped. A start,
    // stop or jump (seek, loop) ends what plays. A block covers the beats that round to its own
    // samples: from half a sample before its first to half a sample before its end, so a note is
    // never a sample early and never played twice.
    const double half = 0.5 * m_beatsPerSample;
    double start;
    if (context.playing) {
        const bool jump = !m_wasPlaying || std::abs(context.beat - half - m_expectedBeat) > half + 1e-9;
        m_blockStart = context.beat;
        if (jump) { stopAll(0); m_strip = -1; m_held = false; m_gridOrigin = 0; m_zoneSelect = Middle; start = context.beat - half; }
        else start = m_expectedBeat;
    } else {
        m_blockStart = m_freeBeat;
        if (m_wasPlaying) { stopAll(0); m_strip = -1; m_held = false; m_zoneSelect = Middle; }
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
        // An event and a pattern note at the same instant: the event first (a press on the beat
        // is heard on that beat), so the notes before it stop a hair short of it.
        const double at = std::max(cursor, m_blockStart + e.offset * m_beatsPerSample);
        const double before = std::max(cursor, at - 1e-9);
        releaseDue(cursor, before);
        pedalDue(cursor, before);
        playSteps(cursor, before);
        cursor = before;
        bool bass = false;
        int zone = -1;
        const bool note = isNoteOn(e) || isNoteOff(e);
        const int strip = note ? stripOfKey(e.data1, bass, zone) : -1;
        const int zoneKey = note ? zoneOfKey(e.data1) : -1;
        if (zoneKey >= 0) {
            if (isNoteOn(e)) m_zoneSelect = zoneKey;                   // for the middle-octave presses after it
        } else if (strip >= 0 && isNoteOn(e)) {
            press(strip, bass, zone >= 0 ? zone : m_zoneSelect, e.data1, e.data2, at, e.offset);
        } else if (strip >= 0) {
            releaseTrigger(e.data1, e.offset);
        } else {
            if (isAllOff(e)) { stopAll(e.offset); m_strip = -1; m_held = false; }
            if (m_outCount < MaxEvents) m_out[std::size_t(m_outCount++)] = e;   // not ours: to the piano
        }
    }
    releaseDue(cursor, end);
    pedalDue(cursor, end);
    playSteps(cursor, end);

    for (int i = 0; i < m_outCount; ++i) context.midi->add(m_out[std::size_t(i)]);
    context.midi->sort();
    if (context.playing) m_expectedBeat = end;
    else m_freeBeat = m_blockStart + m_frames * m_beatsPerSample;
    publishActive();
}
