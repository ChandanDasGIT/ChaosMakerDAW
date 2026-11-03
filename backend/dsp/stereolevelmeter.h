#pragma once

#include "core/valueformat.h"
#include "dsp/levelscale.h"
#include "dsp/peakmeter.h"
#include <array>
#include <string>
#include <string_view>

struct StereoMeterSnapshot {
    std::array<double, 2> db{levelscale::silenceDb, levelscale::silenceDb};
    std::array<double, 2> normalized{0.0, 0.0};
    std::array<std::string, 2> levelText{"\xe2\x88\x92\xe2\x88\x9e", "\xe2\x88\x92\xe2\x88\x9e"};
    std::array<levelscale::Band, 2> classification{levelscale::Band::Normal, levelscale::Band::Normal};
    std::array<bool, 2> clipped{false, false};
    std::string tooltipText;
};

class StereoLevelMeter {
public:
    explicit StereoLevelMeter(std::string label = {});

    const std::string& label() const noexcept { return m_label; }
    void setLabel(std::string label);

    StereoMeterSnapshot update(PeakMeter& left, PeakMeter& right, double elapsedSeconds) noexcept;
    StereoMeterSnapshot updateWithPeaks(float leftPeak, bool leftClipped, float rightPeak, bool rightClipped, double elapsedSeconds) noexcept;
    StereoMeterSnapshot setDirectDb(double leftDb, double rightDb, bool leftClipped = false, bool rightClipped = false) noexcept;

    void reset() noexcept;

    const StereoMeterSnapshot& currentSnapshot() const noexcept { return m_snapshot; }

    static std::string formatTooltip(std::string_view label, double leftDb, double rightDb);

private:
    std::string m_label;
    std::array<double, 2> m_currentDb{levelscale::silenceDb, levelscale::silenceDb};
    StereoMeterSnapshot m_snapshot;
};
