#include "mixerpanel.h"

int mixer::chooseTargetClip(const std::vector<int>& trackClipIds, int selectedClipId) noexcept
{
    if (trackClipIds.empty()) return -1;
    if (selectedClipId >= 0
        && std::find(trackClipIds.begin(), trackClipIds.end(), selectedClipId) != trackClipIds.end())
        return selectedClipId;
    return trackClipIds.front();
}

namespace mixer {

double clampVolume(double gain) noexcept
{
    if (!std::isfinite(gain) || gain < minVolumeGain) return minVolumeGain;
    if (gain > maxVolumeGain) return maxVolumeGain;
    return gain;
}

double clampClipGain(double gainDb) noexcept
{
    if (!std::isfinite(gainDb) || gainDb < minClipGainDb) return minClipGainDb;
    if (gainDb > maxClipGainDb) return maxClipGainDb;
    return gainDb;
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

std::string formatMasterVolumeTooltip(double gain)
{
    const int percent = static_cast<int>(std::round(clampVolume(gain) * 100.0));
    return "Master volume " + std::to_string(percent) + "%; double-click for 0 dB";
}

std::string formatClipGainDb(double gainDb)
{
    if (!std::isfinite(gainDb)) {
        return "\xe2\x80\x94"; // "—"
    }
    const double rounded = std::round(gainDb * 10.0) / 10.0;
    if (rounded > 0.0) {
        return "+" + valueformat::withUnit(rounded, 1, "dB");
    }
    return valueformat::withUnit(rounded, 1, "dB");
}

std::string formatClipGainTooltip(double gainDb)
{
    if (!std::isfinite(gainDb)) {
        return "No clip selected";
    }
    const std::string val = formatClipGainDb(gainDb);
    return "Clip gain " + val + "; double-click for 0 dB";
}

namespace {
int panPercent(double pan) { return static_cast<int>(std::lround(panlaw::clampPan(pan) * 100.0)); }
}

std::string formatPan(double pan)
{
    const int percent = panPercent(pan);
    if (percent == 0) return "C";
    return (percent < 0 ? "L" : "R") + std::to_string(std::abs(percent));
}

std::string formatPanTooltip(double pan)
{
    const int percent = panPercent(pan);
    if (percent == 0) return "Pan centre; double-click to centre";
    return "Pan " + std::to_string(std::abs(percent)) + "% " + (percent < 0 ? "left" : "right")
        + "; double-click to centre";
}

std::string formatMuteTooltip(bool muted)
{
    return muted ? "Mute enabled" : "Mute disabled";
}

std::string formatMasterMuteTooltip(bool muted)
{
    return muted ? "Master mute enabled" : "Master mute disabled";
}

std::string formatSoloTooltip(bool solo)
{
    return solo ? "Solo enabled" : "Solo disabled";
}

StripState MixerPanel::makeStrip(int trackId, std::string name, std::string colour,
                                 bool muted, bool solo, double volume,
                                 double stripWidth, double clipGain)
{
    StripState state;
    state.trackId = trackId;
    state.name = std::move(name);
    state.colour = std::move(colour);
    state.isMuted = muted;
    state.isSolo = solo;
    state.volume = clampVolume(volume);
    state.clipGain = clampClipGain(clipGain);
    state.stripWidth = clampStripWidth(stripWidth);
    state.volumeText = formatVolumeDb(state.volume);
    state.volumeTooltip = formatVolumeTooltip(state.volume);
    state.clipGainText = formatClipGainDb(state.clipGain);
    state.clipGainTooltip = formatClipGainTooltip(state.clipGain);
    state.muteTooltip = formatMuteTooltip(state.isMuted);
    state.soloTooltip = formatSoloTooltip(state.isSolo);
    return state;
}

MasterStripState MixerPanel::makeMasterStrip(bool muted, double volume, double stripWidth)
{
    MasterStripState state;
    state.isMuted = muted;
    state.volume = clampVolume(volume);
    state.stripWidth = clampStripWidth(stripWidth);
    state.volumeText = formatVolumeDb(state.volume);
    state.volumeTooltip = formatMasterVolumeTooltip(state.volume);
    state.muteTooltip = formatMasterMuteTooltip(state.isMuted);
    return state;
}

} // namespace mixer
