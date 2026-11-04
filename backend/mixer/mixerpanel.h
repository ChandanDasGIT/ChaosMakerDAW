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

inline constexpr double minClipGainDb = -24.0;
inline constexpr double maxClipGainDb = 24.0;
inline constexpr double defaultClipGainDb = 0.0;
inline constexpr double clipGainStep = 0.5;

inline constexpr double minStripWidth = 82.0;
inline constexpr double maxStripWidth = 220.0;
inline constexpr double defaultStripWidth = 108.0;

double clampVolume(double gain) noexcept;
double clampClipGain(double gainDb) noexcept;
double clampStripWidth(double width) noexcept;

double volumeToDb(double gain) noexcept;
double dbToVolume(double db) noexcept;

std::string formatVolumeDb(double gain);
std::string formatVolumeTooltip(double gain);
std::string formatMasterVolumeTooltip(double gain);
std::string formatClipGainDb(double gainDb);
std::string formatClipGainTooltip(double gainDb);
std::string formatMuteTooltip(bool muted);
std::string formatMasterMuteTooltip(bool muted);
std::string formatSoloTooltip(bool solo);

struct StripState {
    int trackId = -1;
    std::string name;
    std::string colour;
    bool isMuted = false;
    bool isSolo = false;
    double volume = defaultVolumeGain;
    double clipGain = defaultClipGainDb;
    double stripWidth = defaultStripWidth;
    std::string volumeText;
    std::string volumeTooltip;
    std::string clipGainText;
    std::string clipGainTooltip;
    std::string muteTooltip;
    std::string soloTooltip;
};

struct MasterStripState {
    std::string name = "Master";
    std::string colour = "#60cdff";
    bool isMuted = false;
    double volume = defaultVolumeGain;
    double stripWidth = defaultStripWidth;
    std::string volumeText;
    std::string volumeTooltip;
    std::string muteTooltip;
};

class MixerPanel {
public:
    MixerPanel() = default;

    static StripState makeStrip(int trackId, std::string name, std::string colour,
                                bool muted, bool solo, double volume,
                                double stripWidth = defaultStripWidth,
                                double clipGain = defaultClipGainDb);

    static MasterStripState makeMasterStrip(bool muted, double volume,
                                           double stripWidth = defaultStripWidth);
};

} // namespace mixer
