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
// MIDI keyboard and recorded clips all drive it the same way:
//   chord area (both hands): the white keys C4 .. C5   (60 62 64 65 67 69 71 72)
//   bass area (bass only):   the white keys C2 .. C3   (36 38 40 41 43 45 47 48)
// Every other MIDI event passes through to the piano unchanged.
//
// Autoplay off: the chord (or bass) sounds while its key is held. Autoplay 1 .. 4: a press
// starts the strip's pattern, which keeps going after the key is let go (latched) until another
// strip is pressed, the same strip is pressed again (it stops), autoplay is turned off, the
// transport starts, stops or jumps, or an all-notes-off arrives. A press strikes the chord at
// once; the pattern then runs on the bar grid: the song's while the transport plays, its own
// (from the press, at the project tempo) while it is stopped. Chord changes are voice-led.
//
// process() runs on the audio thread: no allocation, no locks. setStrips() / setAutoplay() may
// be called from any thread; the player takes them at its next block.
class ChordPlayer {
public:
    static constexpr std::array<int, 8> ChordKeys{60, 62, 64, 65, 67, 69, 71, 72};
    static constexpr std::array<int, 8> BassKeys{36, 38, 40, 41, 43, 45, 47, 48};
    static constexpr int Strips = 8;

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
    int autoplay() const noexcept { return m_autoplay.load(std::memory_order_relaxed); }

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

    // The strip and area a key plays, or strip -1.
    static int stripOfKey(int key, bool& bass) noexcept;

private:
    struct Sounding { int key = -1; double offBeat = 0; };
    static constexpr int MaxSounding = 48;
    static constexpr int MaxEvents = 1024;

    void pullSettings() noexcept;
    void rebuildPattern(int numerator, int denominator) noexcept;
    void press(int strip, bool bass, int velocity, double beat, int offset) noexcept;
    void releaseTrigger(int strip, bool bass, int offset) noexcept;
    void stopAll(int offset) noexcept;                    // note-offs for everything sounding
    void releaseDue(double fromBeat, double toBeat) noexcept;
    void playSteps(double fromBeat, double toBeat) noexcept;
    void noteOn(int key, int velocity, double beat, double lengthBeats) noexcept;
    void send(int offset, std::uint8_t status, int key, int velocity) noexcept;
    int offsetAt(double beat) const noexcept;
    int keyFor(int voice) const noexcept;
    void publishActive() noexcept;

    // Settings, written by any thread.
    std::array<std::atomic<int>, Strips * 6> m_shared{};   // per strip: count, bass, tones[4]
    std::atomic<std::uint32_t> m_generation{1};
    std::atomic<int> m_autoplay{3};
    // Settings in use (audio thread).
    std::array<Strip, Strips> m_strips{};
    std::uint32_t m_seenGeneration = 0;

    // Pattern for the current meter, autoplay and area.
    chords::Pattern m_pattern{};
    int m_patternMode = -1, m_patternNumerator = 0, m_patternDenominator = 0;
    bool m_patternBass = false;

    // State.
    double m_sampleRate = 48000.0;
    double m_blockStart = 0, m_beatsPerSample = 0;
    int m_frames = 0;
    bool m_wasPlaying = false;
    double m_expectedBeat = 0;                            // where the next block should start (transport)
    double m_freeBeat = 0;                                // the free clock while stopped
    double m_gridOrigin = 0;                              // beat where bar 1 of the pattern starts
    int m_strip = -1;                                     // the strip latched or held
    bool m_bass = false;
    bool m_held = false;                                  // autoplay off: the key is down
    int m_velocity = 100;
    double m_entryBeat = -1e9;                            // the press's own strike
    chords::Voicing m_voicing{};
    int m_bassNote = -1;
    std::array<Sounding, MaxSounding> m_sounding{};
    std::array<MidiEvent, MaxEvents> m_out{};
    int m_outCount = 0;
    std::array<MidiEvent, MaxEvents> m_in{};

    std::atomic<bool> m_running{false};
    std::atomic<int> m_activeStrip{-1};
    std::atomic<bool> m_activeBass{false};
};
