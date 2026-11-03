#pragma once

#include "core/valueformat.h"
#include "dsp/levelscale.h"
#include "dsp/peakmeter.h"
#include <string>

struct MeterSnapshot {
    double db = levelscale::silenceDb;
    double normalized = 0.0;
    std::string levelText = "\xe2\x88\x92\xe2\x88\x9e"; // "−∞"
    levelscale::Band classification = levelscale::Band::Normal;
    bool clipped = false;
};

class LevelMeter {
public:
    explicit LevelMeter(std::string label = {}, bool showTarget = false, double calibrationTargetDb = levelscale::warningDb);

    const std::string& label() const noexcept { return m_label; }
    void setLabel(std::string label) { m_label = std::move(label); }

    bool showTarget() const noexcept { return m_showTarget; }
    void setShowTarget(bool show) noexcept { m_showTarget = show; }

    double calibrationTargetDb() const noexcept { return m_calibrationTargetDb; }
    void setCalibrationTargetDb(double db) noexcept { m_calibrationTargetDb = db; }
    double normalizedTarget() const noexcept { return levelscale::normalizedLevel(m_calibrationTargetDb); }

    MeterSnapshot update(PeakMeter& peakMeter, double elapsedSeconds) noexcept;
    MeterSnapshot updateWithPeak(float peakGain, bool clipped, double elapsedSeconds) noexcept;
    MeterSnapshot setDirectDb(double db, bool clipped = false) noexcept;

    void reset() noexcept;

    const MeterSnapshot& currentSnapshot() const noexcept { return m_snapshot; }

private:
    std::string m_label;
    bool m_showTarget{false};
    double m_calibrationTargetDb{levelscale::warningDb};
    double m_currentDb{levelscale::silenceDb};
    MeterSnapshot m_snapshot;
};
