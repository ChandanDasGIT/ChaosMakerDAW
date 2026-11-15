#include "bassvaluetext.h"

#include "common/core/valueformat.h"

std::string bass::valueText(std::string_view kind, double value)
{
    using namespace valueformat;
    if (kind == "percent") return percent(value);
    if (kind == "seconds") return value < 0.005 ? "Off" : rounded(value * 1000) + " ms";
    if (kind == "semitones") return number(value) + " st";
    if (kind == "db") return signedValue(value, 1) + " dB";
    if (kind == "pluck")
        return value < 0.05 ? "Bridge" : value > 0.95 ? "Neck" : rounded(value * 100) + " % to the neck";
    if (kind == "blend")
        return value < 0.05 ? "Neck" : value > 0.95 ? "Bridge"
                                                     : rounded((1 - value) * 100) + " / " + rounded(value * 100);
    return number(value);
}
