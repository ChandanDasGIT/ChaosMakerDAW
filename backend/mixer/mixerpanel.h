#pragma once

#include "core/valueformat.h"
#include "dsp/levelscale.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace mixer {

inline constexpr double minVolumeGain = 0.0;
inline constexpr double maxVolumeGain = 2.0;
inline constexpr double defaultVolumeGain = 1.0;
inline constexpr double volumeStep = 0.01;

inline constexpr double minStripWidth = 82.0;
inline constexpr double maxStripWidth = 220.0;
inline constexpr double defaultStripWidth = 108.0;

double clampVolume(double gain) noexcept;
double clampStripWidth(double width) noexcept;

double volumeToDb(double gain) noexcept;
double dbToVolume(double db) noexcept;

std::string formatVolumeDb(double gain);
std::string formatVolumeTooltip(double gain);
std::string formatMuteTooltip(bool muted);
std::string formatSoloTooltip(bool solo);

struct StripState {
    int trackId = -1;
    std::string name;
    std::string colour;
    bool isMuted = false;
    bool isSolo = false;
    double volume = defaultVolumeGain;
    double stripWidth = defaultStripWidth;
    std::string volumeText;
    std::string volumeTooltip;
    std::string muteTooltip;
    std::string soloTooltip;
};

class MixerPanel {
public:
    MixerPanel() = default;

    static StripState makeStrip(int trackId, std::string name, std::string colour,
                                bool muted, bool solo, double volume,
                                double stripWidth = defaultStripWidth);
};

} // namespace mixer
