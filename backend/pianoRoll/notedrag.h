#pragma once

// What dragging notes in the piano roll may do.
namespace notedrag {
// A note moved by `delta` beats starts no earlier than beat 0.
double movedStart(double start, double delta);
// A note moved by `delta` semitones stays in 0..127.
int movedPitch(int pitch, int delta);
// A note resized by `delta` beats is never shorter than `minimum`.
double resizedLength(double length, double delta, double minimum);
// A note being drawn reaches the grid cell `snappedEnd` lies in (`step` long), never shorter than
// the chosen note length `minimum`.
double drawnLength(double start, double snappedEnd, double step, double minimum);
}
