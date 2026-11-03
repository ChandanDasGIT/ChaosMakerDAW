#include "mixerpanel.h"

namespace mixer {

double clampVolume(double gain) noexcept
{
    if (!std::isfinite(gain) || gain < minVolumeGain) return minVolumeGain;
    if (gain > maxVolumeGain) return maxVolumeGain;
    return gain;
}

double clampStripWidth(double width) noexcept
{
    if (!std::isfinite(width) || width < minStripWidth) return minStripWidth;
    if (width > maxStripWidth) return maxStripWidth;
    return width;
}

double volumeToDb(double gain) noexcept
{
    return levelscale::gainToDb(gain);
}

double dbToVolume(double db) noexcept
{
    return clampVolume(levelscale::dbToGain(db));
}

std::string formatVolumeDb(double gain)
{
    if (gain <= 0.0 || !std::isfinite(gain)) {
        return "\xe2\x88\x92\xe2\x88\x9e dB"; // "−∞ dB"
    }
    return valueformat::withUnit(volumeToDb(gain), 1, "dB");
}

std::string formatVolumeTooltip(double gain)
{
    const int percent = static_cast<int>(std::round(clampVolume(gain) * 100.0));
    return "Track volume " + std::to_string(percent) + "%; double-click for 0 dB";
}

std::string formatMuteTooltip(bool muted)
{
    return muted ? "Mute enabled" : "Mute disabled";
}

std::string formatSoloTooltip(bool solo)
{
    return solo ? "Solo enabled" : "Solo disabled";
}

StripState MixerPanel::makeStrip(int trackId, std::string name, std::string colour,
                                 bool muted, bool solo, double volume,
                                 double stripWidth)
{
    StripState state;
    state.trackId = trackId;
    state.name = std::move(name);
    state.colour = std::move(colour);
    state.isMuted = muted;
    state.isSolo = solo;
    state.volume = clampVolume(volume);
    state.stripWidth = clampStripWidth(stripWidth);
    state.volumeText = formatVolumeDb(state.volume);
    state.volumeTooltip = formatVolumeTooltip(state.volume);
    state.muteTooltip = formatMuteTooltip(state.isMuted);
    state.soloTooltip = formatSoloTooltip(state.isSolo);
    return state;
}

} // namespace mixer
