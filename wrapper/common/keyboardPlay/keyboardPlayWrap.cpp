#include "keyboardPlayWrap.h"

#include "common/midi/keyboardplay.h"

KeyboardPlayWrap::KeyboardPlayWrap(QObject* parent) : QObject(parent) {}

int KeyboardPlayWrap::clickVelocity(double fraction, int lowest) const
{
    return keyboardplay::clickVelocity(fraction, lowest);
}

int KeyboardPlayWrap::shiftedNote(int note, int shift) const
{
    return keyboardplay::shiftedNote(note, shift);
}
