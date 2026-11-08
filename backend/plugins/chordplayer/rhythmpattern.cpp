#include "plugins/chordplayer/rhythmpattern.h"

#include <algorithm>
#include <cmath>

namespace chords {
namespace {

struct Meter {
    double pulse = 1.0;                   // one count, in quarter notes (4/4: 1, 6/8: 0.5)
    int pulses = 4;
    std::array<int, 32> groupStarts{};    // pulses that start a group (where the bass and accents go)
    int groups = 0;
    std::array<int, 32> strong{};         // pulses the bass plays on
    int strongCount = 0;
    double barBeats() const noexcept { return pulses * pulse; }
};

Meter meterOf(int numerator, int denominator) noexcept
{
    Meter m;
    numerator = std::clamp(numerator, 1, 32);
    denominator = (denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16 || denominator == 32)
                      ? denominator : 4;
    m.pulse = 4.0 / denominator;
    m.pulses = numerator;
    const auto addGroup = [&m](int p) { if (m.groups < 32) m.groupStarts[std::size_t(m.groups++)] = p; };
    if (denominator >= 8 && numerator >= 6 && numerator % 3 == 0) {
        for (int p = 0; p < numerator; p += 3) addGroup(p);            // 6/8, 9/8, 12/8: dotted beats
    } else if (denominator >= 8) {
        int p = 0;                                                     // 5/8 = 2+3, 7/8 = 2+2+3 ...
        while (numerator - p > 3) { addGroup(p); p += 2; }
        addGroup(p);
    } else {
        for (int p = 0; p < numerator; ++p) addGroup(p);               // every beat
    }
    const auto addStrong = [&m](int p) { if (m.strongCount < 32) m.strong[std::size_t(m.strongCount++)] = p; };
    if (denominator >= 8) {
        for (int g = 0; g < m.groups; ++g) addStrong(m.groupStarts[std::size_t(g)]);
    } else {
        addStrong(0);
        if (numerator >= 4 && numerator % 2 == 0) addStrong(numerator / 2);   // 4/4: one and three
    }
    return m;
}

bool isGroupStart(const Meter& m, double beat) noexcept
{
    for (int g = 0; g < m.groups; ++g)
        if (std::abs(m.groupStarts[std::size_t(g)] * m.pulse - beat) < 1e-9) return true;
    return false;
}

bool isStrong(const Meter& m, int pulse) noexcept
{
    for (int s = 0; s < m.strongCount; ++s) if (m.strong[std::size_t(s)] == pulse) return true;
    return false;
}

// The beat where the bass played on strong pulse `s` lets go: the next strong pulse or the bar end.
double nextStrongBeat(const Meter& m, int s) noexcept
{
    return s + 1 < m.strongCount ? m.strong[std::size_t(s + 1)] * m.pulse : m.barBeats();
}

// The group start at or before `beat` (arpeggios restart their run on each group in 7/8).
double groupStartBefore(const Meter& m, double beat) noexcept
{
    double start = 0;
    for (int g = 0; g < m.groups; ++g) {
        const double b = m.groupStarts[std::size_t(g)] * m.pulse;
        if (b <= beat + 1e-9) start = b;
    }
    return start;
}

class Writer {
public:
    explicit Writer(Pattern& out) : m_out(out) { m_out.count = 0; }
    void add(double beat, double length, int voice, int velocity) noexcept
    {
        if (m_out.count >= MaxSteps || length <= 0) return;
        m_out.steps[std::size_t(m_out.count++)] = PatternStep{beat, length, voice, std::clamp(velocity, 1, 127)};
    }

private:
    Pattern& m_out;
};

void bassOnStrongBeats(const Meter& m, Writer& w) noexcept
{
    for (int s = 0; s < m.strongCount; ++s) {
        const double beat = m.strong[std::size_t(s)] * m.pulse;
        w.add(beat, (nextStrongBeat(m, s) - beat) * 0.95, VoiceBass, s == 0 ? 100 : 88);
    }
}

} // namespace

void buildPattern(int mode, int numerator, int denominator, Part part, Pattern& out) noexcept
{
    const Meter m = meterOf(numerator, denominator);
    out.barBeats = m.barBeats();
    Writer w(out);
    const double bar = m.barBeats();
    const int accent = 12;

    switch (mode) {
    case 1: {                                                          // Block
        if (part == Part::Bass) {
            for (int p = 0; p < m.pulses; ++p)
                w.add(p * m.pulse, m.pulse * 0.9, VoiceBass, isStrong(m, p) ? 100 : 78);
            break;
        }
        bassOnStrongBeats(m, w);
        for (int p = 0; p < m.pulses; ++p) {
            const double beat = p * m.pulse;
            const bool simple = denominator <= 4;
            if (simple && numerator == 3 && p == 0) continue;           // oom-pah-pah: the bass alone on one
            if (!simple && isGroupStart(m, beat) && p != 0) continue;  // compound: the bass's dotted beats
            if (!simple && p == 0) continue;
            const int velocity = p == 0 ? 96 : isStrong(m, p) ? 86 : 74;
            w.add(beat, m.pulse * 0.9, VoiceChord, velocity);
        }
        break;
    }
    case 2: {                                                          // Ballad
        bassOnStrongBeats(m, w);
        if (part == Part::Bass) break;
        const double firstGroup = m.groups > 1 ? m.groupStarts[1] * m.pulse : bar;
        w.add(0, std::min(firstGroup, 2.0) * 0.95, VoiceChord, 92);
        static constexpr std::array<int, 4> Broken{1, 2, 3, 2};
        int i = 0;
        for (double beat = 0.5; beat < bar - 1e-9; beat += 0.5, ++i)
            w.add(beat, 0.5 * 0.95, Broken[std::size_t(i % 4)], isGroupStart(m, beat) ? 72 + accent : 68);
        break;
    }
    case 3: {                                                          // Arpeggio in eighths
        if (part == Part::Bass) {
            int i = 0;
            for (double beat = 0; beat < bar - 1e-9; beat += 0.5, ++i)
                w.add(beat, 0.5 * 0.9, i % 2 == 0 ? VoiceBass : VoiceBassOctave, isGroupStart(m, beat) ? 98 : 74);
            break;
        }
        bassOnStrongBeats(m, w);
        for (double beat = 0; beat < bar - 1e-9; beat += 0.5) {
            const int index = int(std::lround((beat - groupStartBefore(m, beat)) / 0.5)) % 4;
            w.add(beat, 0.5 * 0.95, index, isGroupStart(m, beat) ? 78 + accent : 76);
        }
        break;
    }
    case 4: {                                                          // Arpeggio up and down in sixteenths
        if (part == Part::Bass) {
            int i = 0;
            for (double beat = 0; beat < bar - 1e-9; beat += 0.5, ++i)
                w.add(beat, 0.5 * 0.9, i % 2 == 0 ? VoiceBass : VoiceBassOctave, isGroupStart(m, beat) ? 98 : 74);
            break;
        }
        bassOnStrongBeats(m, w);
        static constexpr std::array<int, 8> UpDown{0, 1, 2, 3, 4, 3, 2, 1};
        int i = 0;
        for (double beat = 0; beat < bar - 1e-9; beat += 0.25, ++i)
            w.add(beat, 0.25 * 0.95, UpDown[std::size_t(i % 8)], isGroupStart(m, beat) ? 74 + accent : (i % 2 == 0 ? 72 : 62));
        break;
    }
    default:
        break;
    }
    std::stable_sort(out.steps.begin(), out.steps.begin() + out.count,
                     [](const PatternStep& a, const PatternStep& b) { return a.beat < b.beat; });
}

std::string_view autoplayName(int mode) noexcept
{
    switch (mode) {
    case 0: return "Off";
    case 1: return "Block";
    case 2: return "Ballad";
    case 3: return "Arpeggio";
    case 4: return "Arpeggio 16ths";
    default: return "";
    }
}

} // namespace chords
