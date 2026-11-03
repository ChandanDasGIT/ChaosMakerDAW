#include "tunerstate.h"

#include "instruments/guitar/plugins/tuner/standardtuning.h"
#include "instruments/guitar/plugins/tuner/tuner.h"

#include <cmath>

TunerReading TunerState::evaluate(float frequency, unsigned revision, int selectedString, bool bypassed)
{
    const float hz = (bypassed || !std::isfinite(frequency) || frequency <= 0.0f) ? 0.0f : frequency;
    TunerReading result;
    result.frequency = hz;
    result.revision = revision;
    result.note = "—";
    result.cents = 0.0;
    result.targetHz = 0.0;
    result.stringNumber = 0;
    result.targetSelection = selectedString;
    result.direction = "waiting";
    result.inTune = false;
    result.hasPitch = false;

    if (hz <= 0.0f) {
        return result;
    }

    const auto reading = standardtuning::measure(hz, selectedString);
    if (reading.index >= 0 && static_cast<std::size_t>(reading.index) < standardtuning::targets.size()) {
        const auto& target = standardtuning::targets[static_cast<std::size_t>(reading.index)];
        result.note = target.note;
        result.cents = reading.cents;
        result.targetHz = reading.targetHz;
        result.stringNumber = target.stringNumber;
        result.hasPitch = true;
        result.inTune = std::abs(reading.cents) <= InTuneThresholdCents;
        result.direction = result.inTune ? "inTune" : (reading.cents < 0.0 ? "low" : "high");
    }

    return result;
}

TunerReading TunerState::evaluate(const Tuner& tuner, bool bypassed)
{
    return evaluate(tuner.frequency(), tuner.revision(), tuner.selectedString(), bypassed);
}

void TunerState::update(const Tuner& tuner, bool bypassed, double currentTimeSeconds)
{
    m_reading = evaluate(tuner, bypassed);
    m_lastUpdateTime = currentTimeSeconds;
}

bool TunerState::isFresh(unsigned previousRevision, double elapsedSeconds, double maxAgeSeconds) const
{
    if (m_reading.revision != previousRevision) {
        return true;
    }
    return elapsedSeconds >= 0.0 && elapsedSeconds <= maxAgeSeconds;
}
