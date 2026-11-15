#include "notedrag.h"

#include <algorithm>

double notedrag::movedStart(double start, double delta) { return std::max(0.0, start + delta); }

int notedrag::movedPitch(int pitch, int delta) { return std::clamp(pitch + delta, 0, 127); }

double notedrag::resizedLength(double length, double delta, double minimum)
{
    return std::max(minimum, length + delta);
}

double notedrag::drawnLength(double start, double snappedEnd, double step, double minimum)
{
    return std::max(minimum, snappedEnd - start + step);
}
