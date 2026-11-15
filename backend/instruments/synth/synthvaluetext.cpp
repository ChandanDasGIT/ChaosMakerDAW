#include "synthvaluetext.h"

#include "common/core/valueformat.h"

std::string synth::valueText(std::string_view kind, double value)
{
    using namespace valueformat;
    if (kind == "percent") return percent(value);
    if (kind == "hz")
        return value >= 1000 ? fixed(value / 1000, value >= 10000 ? 1 : 2) + " kHz"
                             : fixed(value, value < 100 ? 1 : 0) + " Hz";
    if (kind == "time") return value < 1 ? rounded(value * 1000) + " ms" : fixed(value, 2) + " s";
    if (kind == "seconds") return value < 0.005 ? "Off" : fixed(value, 2) + " s";
    if (kind == "octave") return value == 0 ? "0" : signedValue(value, -1);
    if (kind == "semitones") return signedValue(value, -1) + " st";
    if (kind == "cents") return signedValue(value, 1) + " ct";
    if (kind == "octaves") return signedValue(value, 1) + " oct";
    if (kind == "db") return signedValue(value, 1) + " dB";
    return number(value);
}
