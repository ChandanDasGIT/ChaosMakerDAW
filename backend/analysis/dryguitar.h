#pragma once

#include <vector>

// A dry electric guitar to play through pedalboards (restMap G19): the tone analyzer needs
// something to feed a candidate pedalboard, and has no recording of the user's own guitar.
// Plucked strings by a small string model (a delay loop per string with a loss filter, the
// pick's place and the pickup's place as combs, the pickup's own resonance on top), played as
// a guitarist would: strummed open chords and muted power chords, or a run of single notes
// across the neck. At the level of a guitar plugged straight in (about -30 dBFS RMS).
// The same sound every time. Hand-written; no recording is used.
namespace analysis {

constexpr double DryGuitarSeconds = 6.0;

std::vector<float> dryGuitarChords(double sampleRate);
std::vector<float> dryGuitarSolo(double sampleRate);

} // namespace analysis
