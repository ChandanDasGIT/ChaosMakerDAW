#include "common/trackEffectsPanel/instruments/guitar/electric/pedaleditorrules.h"

#include "common/core/valueformat.h"
#include "common/dsp/tempodivision.h"

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
    if (type == "pedal.cabinet" && id == "model") return 1.0; // built-in cab models
    return 0.0;
}

std::string pedaleditor::formatValue(double value, const std::string& unit)
{
    if (unit == "channel") {   // the amp's channel switch
        static const char* names[] = {"Clean", "Crunch", "High Gain"};
        return names[std::clamp(int(std::lround(value)), 0, 2)];
    }
    if (unit == "cabmodel") {  // the cabinet's built-in models
        static const char* names[] = {"None", "1x12 Open", "2x12", "4x12 Closed"};
        return names[std::clamp(int(std::lround(value)), 0, 3)];
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

int pedaleditor::slotWhileDragging(int index, int from, int to)
{
    if (from < 0 || to < 0) return index;
    if (index == from) return to;
    if (from < to && index > from && index <= to) return index - 1;
    if (to < from && index >= to && index < from) return index + 1;
    return index;
}
