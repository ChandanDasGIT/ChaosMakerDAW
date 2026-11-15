#include "tunertext.h"

#include "common/core/valueformat.h"
#include "common/trackEffectsPanel/instruments/guitar/electric/tuner/standardtuning.h"

#include <cmath>

std::vector<std::string> tunertext::stringLabels()
{
    std::vector<std::string> labels;
    for (const auto& target : standardtuning::targets)
        labels.push_back(std::to_string(target.stringNumber) + " · " + target.note);
    return labels;
}

std::string tunertext::referenceText() { return "Standard tuning · A4 = 440 Hz"; }

bool tunertext::inTune(double cents) noexcept { return std::isfinite(cents) && std::abs(cents) <= inTuneCents; }

std::string tunertext::status(bool hasPitch, bool bypassed, double cents)
{
    if (!hasPitch) return bypassed ? "Tuner bypassed" : "Play one open string";
    if (inTune(cents)) return "In tune";
    return cents < 0 ? "LOW / FLAT · Tune up ↑" : "HIGH / SHARP · Tune down ↓";
}

std::string tunertext::formatCents(double cents)
{
    return (cents > 0 ? "+" : "") + valueformat::fixed(cents, 1) + " cents";
}

std::string tunertext::formatFrequency(double hz, double targetHz)
{
    return valueformat::fixed(hz, 1) + " Hz → " + valueformat::fixed(targetHz, 2) + " Hz";
}
