#include "valueFormatWrap.h"

#include "common/core/valueformat.h"
#include "common/midi/notenames.h"

ValueFormatWrap::ValueFormatWrap(QObject* parent) : QObject(parent) {}

QString ValueFormatWrap::signedValue(double value, int decimals) const
{
    return QString::fromStdString(valueformat::signedValue(value, decimals));
}

QString ValueFormatWrap::signedDb(double value) const
{
    return QString::fromStdString(valueformat::signedValue(value, 1) + " dB");
}

QString ValueFormatWrap::percent(double fraction) const
{
    return QString::fromStdString(valueformat::percent(fraction));
}

QString ValueFormatWrap::clock(double seconds) const
{
    return QString::fromStdString(valueformat::clock(seconds));
}

QString ValueFormatWrap::noteName(int note) const
{
    return QString::fromStdString(notenames::name(note));
}

QString ValueFormatWrap::pitchName(int note) const
{
    return QString::fromStdString(notenames::pitchName(note));
}

bool ValueFormatWrap::isBlackKey(int note) const
{
    return notenames::isBlackKey(note);
}
