#include "instruments/guitar/pedaleditorrules.h"

#include "core/valueformat.h"
#include "instruments/guitar/dsp/tempodivision.h"
#include "instruments/guitar/plugins/tuner/standardtuning.h"

#include <algorithm>
#include <cmath>

namespace {
bool tempoSynced(const std::string& type) { return type == "pedal.tremolo" || type == "pedal.phaser"; }
}

pedaleditor::Features pedaleditor::features(const std::string& type)
{
    Features f;
    f.pitchDelayNote = type == "pedal.octaver" || type == "pedal.pitchshift";
    f.cabinetIr = type == "pedal.cabinet";
    f.tuner = type == "pedal.tuner";
    f.sustainToggle = type == "pedal.tremolo";
    f.tempoSync = tempoSynced(type);
    return f;
}

bool pedaleditor::isDial(const std::string& type, const std::string& id)
{
    if (type == "pedal.tuner") return false;
    if (tempoSynced(type)) return id != "sync" && id != "division" && id != "sustain";
    return true;
}

bool pedaleditor::disabledWhenSynced(const std::string& type, const std::string& id)
{
    return tempoSynced(type) && id == "rate";
}

double pedaleditor::dialStep(const std::string& type, const std::string& id)
{
    if (type == "pedal.pitchshift" && id == "semitones") return 1.0;
    if (type == "pedal.amp" && id == "channel") return 1.0;   // Clean / Crunch / High Gain
    return 0.0;
}

std::string pedaleditor::formatValue(double value, const std::string& unit)
{
    if (unit == "channel") {   // the amp's channel switch
        static const char* names[] = {"Clean", "Crunch", "High Gain"};
        return names[std::clamp(int(std::lround(value)), 0, 2)];
    }
    const int decimals = unit == "ms" || unit == "st" ? 0 : 2;
    const auto number = valueformat::fixed(value, decimals);
    return unit.empty() ? number : number + " " + unit;
}

std::string pedaleditor::formatTrim(double db)
{
    return (db > 0 ? "+" : "") + valueformat::fixed(db, 1) + " dB";
}

std::vector<std::string> pedaleditor::beatDivisionNames()
{
    return {tempo::divisionNames.begin(), tempo::divisionNames.end()};
}

std::string pedaleditor::syncText(bool synced, double bpm)
{
    return synced ? valueformat::number(bpm) + " BPM · follows bottom-bar tempo"
                  : "Free rate · adjust Rate below";
}

std::vector<std::string> pedaleditor::tunerStringLabels()
{
    std::vector<std::string> labels;
    for (const auto& target : standardtuning::targets)
        labels.push_back(std::to_string(target.stringNumber) + " · " + target.note);
    return labels;
}

std::string pedaleditor::tunerReferenceText() { return "Standard tuning · A4 = 440 Hz"; }

bool pedaleditor::inTune(double cents) noexcept { return std::isfinite(cents) && std::abs(cents) <= inTuneCents; }

std::string pedaleditor::tunerStatus(bool hasPitch, bool bypassed, double cents)
{
    if (!hasPitch) return bypassed ? "Tuner bypassed" : "Play one open string";
    if (inTune(cents)) return "In tune";
    return cents < 0 ? "LOW / FLAT · Tune up ↑" : "HIGH / SHARP · Tune down ↓";
}

std::string pedaleditor::formatCents(double cents)
{
    return (cents > 0 ? "+" : "") + valueformat::fixed(cents, 1) + " cents";
}

std::string pedaleditor::formatTunerFrequency(double hz, double targetHz)
{
    return valueformat::fixed(hz, 1) + " Hz → " + valueformat::fixed(targetHz, 2) + " Hz";
}
