#include "keyboardplay.h"

#include <algorithm>
#include <cmath>

int keyboardplay::clickVelocity(double fraction, int lowest)
{
    const double velocity = std::floor(lowest + (127 - lowest) * fraction + 0.5);
    return int(std::clamp(velocity, 30.0, 127.0));
}

int keyboardplay::shiftedNote(int note, int shift)
{
    const int sent = note - shift;
    return note >= 0 && sent >= 0 && sent <= 127 ? sent : -1;
}
