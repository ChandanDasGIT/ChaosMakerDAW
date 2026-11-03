#include "trackeffectspanel.h"
#include "instruments/instrumentcatalog.h"

bool TrackEffectsPanel::checkGuitarSuiteSupport(std::string_view trackType, bool hasLiveInput)
{
    if (!hasLiveInput) {
        return false;
    }
    if (trackType == "Vocal") {
        return false;
    }
    const auto* inst = instrumentcatalog::find(trackType);
    if (inst) {
        return inst->supportsGuitarSuite;
    }
    return hasLiveInput && trackType != "Vocal";
}

std::string TrackEffectsPanel::validateSelectedEffect(std::string_view effectKey, bool supportsGuitarSuite)
{
    if (effectKey == "pedals" && supportsGuitarSuite) {
        return "pedals";
    }
    return "eq";
}

std::string TrackEffectsPanel::recordModeDescription(bool recordDry)
{
    return recordDry
        ? "Dry recording · current effects are applied during playback"
        : "Print FX · effects are baked into new recordings";
}

std::vector<EffectEntry> TrackEffectsPanel::availableEntries(bool supportsGuitarSuite)
{
    std::vector<EffectEntry> entries;
    entries.push_back({"eq", "Equalizer", "10-band tone shaping", true});
    if (supportsGuitarSuite) {
        entries.push_back({"pedals", "GuitarSuite", "Pedals and signal chain", true});
    }
    return entries;
}

bool TrackEffectsPanel::supportsGuitarSuite() const
{
    return checkGuitarSuiteSupport(m_trackType, m_hasLiveInput);
}

void TrackEffectsPanel::setSelectedEffect(std::string_view key)
{
    m_selectedEffect = validateSelectedEffect(key, supportsGuitarSuite());
}

void TrackEffectsPanel::setTrackInfo(
    int trackId,
    std::string trackName,
    std::string trackType,
    bool hasLiveInput,
    bool recordDry)
{
    m_trackId = trackId;
    m_trackName = std::move(trackName);
    m_trackType = std::move(trackType);
    m_hasLiveInput = hasLiveInput;
    m_recordDry = recordDry;
    m_selectedEffect = validateSelectedEffect(m_selectedEffect, supportsGuitarSuite());
}

std::vector<EffectEntry> TrackEffectsPanel::effectEntries() const
{
    return availableEntries(supportsGuitarSuite());
}

std::string TrackEffectsPanel::currentRecordModeDescription() const
{
    return recordModeDescription(m_recordDry);
}
