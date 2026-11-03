#pragma once

#include <string>

class Tuner;

struct TunerReading {
    float frequency = 0.0f;
    unsigned revision = 0;
    std::string note = "—";
    double cents = 0.0;
    double targetHz = 0.0;
    int stringNumber = 0;
    int targetSelection = 0;
    std::string direction = "waiting";
    bool inTune = false;
    bool hasPitch = false;
};

class TunerState {
public:
    static constexpr double InTuneThresholdCents = 3.0;

    static TunerReading evaluate(float frequency, unsigned revision, int selectedString, bool bypassed);
    static TunerReading evaluate(const Tuner& tuner, bool bypassed);

    void update(const Tuner& tuner, bool bypassed, double currentTimeSeconds = 0.0);
    const TunerReading& current() const { return m_reading; }
    bool isFresh(unsigned previousRevision, double elapsedSeconds, double maxAgeSeconds = 0.5) const;

private:
    TunerReading m_reading;
    double m_lastUpdateTime = 0.0;
};
