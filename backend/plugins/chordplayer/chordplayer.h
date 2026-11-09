#pragma once

#include "plugins/chordplayer/chordvoicer.h"
#include "plugins/chordplayer/rhythmpattern.h"
#include "processor/processor.h"

#include <array>
#include <atomic>
#include <cstdint>

// The Chords track's player: it turns "strip N pressed" into a pianist's notes, in time.
//
// The eight strips are played by MIDI notes on fixed keys, so the on-screen strips, KEYS, a
// MIDI keyboard and recorded clips all drive it the same way. A strip's chord area has three
// zones, the same chord an octave apart, set by its root (C: C3-C5, C4-C6, C5-C7; voice-led
// within the zone); the bass part always stays in C2 .. C3:
//   chord area, middle zone: the white keys C4 .. C5   (60 62 64 65 67 69 71 72)
//   chord area, low zone:    the white keys C0 .. C1   (12 14 16 17 19 21 23 24)
//   chord area, high zone:   the white keys C6 .. C7   (84 86 88 89 91 93 95 96)
//   bass area (bass only):   the white keys C2 .. C3   (36 38 40 41 43 45 47 48)
//   zone keys:               F#2 G#2 A#2 (42 44 46) = low / middle / high for the presses on
//                            C4 .. C5 that follow (back to middle when the transport starts,
//                            stops or jumps)
// Both ways of naming a zone are always understood; the app's preference only chooses which
// one the strips write (triggerNotes). Every other MIDI event passes through unchanged.
//
// Autoplay off: the chord (or bass) sounds while its key is held. Autoplay 1 .. 4: a press
// starts the strip's pattern (two bars, see rhythmpattern.h), which keeps going after the key is
// let go (latched) until another strip is pressed, the same strip is pressed again (it stops),
// autoplay is turned off, the transport starts, stops or jumps, or an all-notes-off arrives. The
// pattern runs on the grid: the song's while the transport plays, its own (from the press, at
// the project tempo) while it is stopped, from the first press. A first press joins the grid
// (stopped: the pattern starts at the press, from its first note; playing: at the step nearest
// the press). Another strip while a pattern plays is a seamless change: nothing is struck, the
// grid goes on, the old notes ring until the pattern's next note and the new chord comes in on
// it (a press a hair late for a note plays that note again at once, with the new chord).
// Dynamics: every note the same volume, or the chord's top note (and in lines, every note of
// its name) louder. Bass with chords off: the chord area plays the right hand only. Chord changes are
// voice-led. The pedalled patterns lift the sustain pedal (CC 64) at each press and put it down
// again just after.
//
// process() runs on the audio thread: no allocation, no locks. setStrips() / setAutoplay() may
// be called from any thread; the player takes them at its next block.
class ChordPlayer {
public:
    static constexpr std::array<int, 8> ChordKeys{60, 62, 64, 65, 67, 69, 71, 72};
    static constexpr std::array<int, 8> BassKeys{36, 38, 40, 41, 43, 45, 47, 48};
    static constexpr std::array<int, 8> LowChordKeys{12, 14, 16, 17, 19, 21, 23, 24};
    static constexpr std::array<int, 8> HighChordKeys{84, 86, 88, 89, 91, 93, 95, 96};
    static constexpr std::array<int, 3> ZoneKeys{42, 44, 46};
    static constexpr int Strips = 8;
    enum Zone { Low = 0, Middle = 1, High = 2 };

    // How a press names its zone: its own octave, or a zone key before a middle-octave press.
    enum class KeyMapping { Octaves, ZoneKeys };
    struct Trigger {
        std::array<int, 2> keys{};        // sent in this order; a zone key is a short note
        int count = 0;
        int stripKey = -1;                // the key held while the strip is held
    };
    static Trigger triggerNotes(int strip, bool bass, int zone, KeyMapping mapping) noexcept;

    // One strip's chord as the player needs it: up to four tones (pitch classes, root first)
    // and the bass pitch class. count 0: an empty strip (pressing it does nothing).
    struct Strip {
        std::array<int, 4> tones{};
        int count = 0;
        int bass = -1;
    };
    static Strip stripOf(const chords::Chord& chord);

    ChordPlayer();

    void setStrips(const std::array<Strip, Strips>& strips) noexcept;
    void setAutoplay(int mode) noexcept;                  // 0 .. 4
    void setDynamics(int dynamics) noexcept;              // chords::DynamicsEven / DynamicsTopNote
    void setBassWithChords(bool on) noexcept;
    int autoplay() const noexcept { return m_autoplay.load(std::memory_order_relaxed); }
    int dynamics() const noexcept { return m_dynamics.load(std::memory_order_relaxed); }
    bool bassWithChords() const noexcept { return m_bassWithChords.load(std::memory_order_relaxed); }

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;                                // forget everything (no events)
    // Rewrites context.midi in place: trigger keys become the pattern's notes.
    void process(ProcessContext& context) noexcept;

    // A latched pattern is playing: the track must keep running with the transport stopped.
    bool running() const noexcept { return m_running.load(std::memory_order_relaxed); }
    // For the strips' lights, from any thread: the strip playing (or held), -1 for none, and
    // whether it is its bass area.
    int activeStrip() const noexcept { return m_activeStrip.load(std::memory_order_relaxed); }
    bool activeBass() const noexcept { return m_activeBass.load(std::memory_order_relaxed); }
    int activeZone() const noexcept { return m_activeZone.load(std::memory_order_relaxed); }
    // The meter the pattern was last built for (0 before the first block). Read it on the audio
    // thread, or where nothing else drives the player (tests).
    int patternNumerator() const noexcept { return m_patternNumerator; }
    int patternDenominator() const noexcept { return m_patternDenominator; }

    // The strip and area a key plays, or strip -1. `zone`: Low or High for those octaves, -1 for
    // the middle octave (the zone keys decide; Middle unless one was pressed) and the bass.
    static int stripOfKey(int key, bool& bass, int& zone) noexcept;
    static int stripOfKey(int key, bool& bass) noexcept;
    static int zoneOfKey(int key) noexcept;               // a zone key's zone, or -1

private:
    struct Sounding { int key = -1; double offBeat = 0; };
    static constexpr int MaxSounding = 48;
    static constexpr int MaxEvents = 1024;

    void pullSettings() noexcept;
    void rebuildPattern(int numerator, int denominator) noexcept;
    void press(int strip, bool bass, int zone, int key, int velocity, double beat, int offset) noexcept;
    void releaseTrigger(int key, int offset) noexcept;
    void stopAll(int offset) noexcept;                    // note-offs for everything sounding
    void releaseDue(double fromBeat, double toBeat) noexcept;
    void pedalDue(double fromBeat, double toBeat) noexcept;
    void playSteps(double fromBeat, double toBeat) noexcept;
    void playStep(const chords::PatternStep& step, double at, double fromBeat, double toBeat) noexcept;
    int velocityOf(bool top, int accent = chords::AccentBeat) const noexcept;   // the dynamics, scaled by the press
    void placeChord() noexcept;                           // the voicing, bass and tones for the strip
    void noteOn(int key, int velocity, double beat, double lengthBeats) noexcept;
    void send(int offset, std::uint8_t status, int key, int velocity) noexcept;
    int offsetAt(double beat) const noexcept;
    int keyFor(const chords::PatternStep& step) const noexcept;
    bool leftHandAt(double patternBeat) const noexcept;   // a bass-part step at this point of the pattern
    double untilLeftHand(double patternBeat) const noexcept;   // beats to the pattern's next one
    int inBass(int key) const noexcept;                   // folded into the bass note's octave
    void publishActive() noexcept;

    // Settings, written by any thread.
    std::array<std::atomic<int>, Strips * 6> m_shared{};   // per strip: count, bass, tones[4]
    std::atomic<std::uint32_t> m_generation{1};
    std::atomic<int> m_autoplay{3};
    std::atomic<int> m_dynamics{0};
    std::atomic<bool> m_bassWithChords{true};
    // Settings in use (audio thread).
    std::array<Strip, Strips> m_strips{};
    std::uint32_t m_seenGeneration = 0;

    // Pattern for the current meter, autoplay and area.
    chords::Pattern m_pattern{};
    int m_patternMode = -1, m_patternNumerator = 0, m_patternDenominator = 0;
    chords::Part m_patternPart = chords::Part::Chord;
    int m_numerator = 4, m_denominator = 4;               // the meter of this block

    // State.
    double m_sampleRate = 48000.0;
    double m_blockStart = 0, m_beatsPerSample = 0, m_beatsPerMs = 0;
    int m_frames = 0;
    bool m_wasPlaying = false;
    double m_expectedBeat = 0;                            // where the next block should start (transport)
    double m_freeBeat = 0;                                // the free clock while stopped
    double m_gridOrigin = 0;                              // beat where bar 1 of the pattern starts
    int m_strip = -1;                                     // the strip latched or held
    bool m_bass = false;
    int m_zone = Middle;                                  // its zone
    int m_triggerKey = -1;                                // the key that pressed it
    int m_zoneSelect = Middle;                            // from the zone keys
    bool m_held = false;                                  // autoplay off: the key is down
    int m_velocity = 100;
    double m_entryGrid = -1e9;                            // the grid step a press joined
    double m_entryShift = 0;                              // the press's beat minus that step's
    double m_entryBeat = 0;                               // the press's beat
    chords::Voicing m_voicing{};
    int m_bassNote = -1;
    int m_rootNote = -1;                                  // the root at the bottom of the voicing
    std::array<int, 4> m_toneOffsets{};                   // each chord tone above the root, rising
    std::array<int, 4> m_toneClasses{};
    int m_toneCount = 0;
    bool m_pedalDown = false, m_pedalPending = false;
    bool m_changePending = false;                         // a seamless change waits for the next note
    bool m_rootPending = false;                           // and its first left-hand note is the root
    double m_pedalAt = 0;                                 // where the pending pedal goes down
    std::array<Sounding, MaxSounding> m_sounding{};
    std::array<MidiEvent, MaxEvents> m_out{};
    int m_outCount = 0;
    std::array<MidiEvent, MaxEvents> m_in{};

    std::atomic<bool> m_running{false};
    std::atomic<int> m_activeStrip{-1};
    std::atomic<bool> m_activeBass{false};
    std::atomic<int> m_activeZone{Middle};
};
