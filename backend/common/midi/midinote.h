#pragma once

// One note of a pattern clip. Pure domain data (no Qt).
struct MidiNote {
    int id = 0;
    int note = 60; // 0..127 MIDI note number
    double startBeat = 0.0;
    double lengthBeats = 1.0;
    int velocity = 100; // 0..127
    bool selected = false; // editor state only; never stored in clips or history

    bool operator==(const MidiNote&) const = default;
};
