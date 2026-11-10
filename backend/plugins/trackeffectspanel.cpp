#include "trackeffectspanel.h"
#include "plugins/effectchain.h"

#include <algorithm>

std::string TrackEffectsPanel::validateSelectedEffect(std::string_view effectKey, std::span<const std::string> chain)
{
    if (std::find(chain.begin(), chain.end(), effectKey) != chain.end()) return std::string(effectKey);
    return chain.empty() ? std::string() : chain.front();
}

std::string TrackEffectsPanel::recordModeDescription(bool recordDry)
{
    return recordDry
        ? "Dry recording · current effects are applied during playback"
        : "Print FX · effects are baked into new recordings";
}

bool TrackEffectsPanel::supportsGuitarSuite() const
{
    return std::find(m_chain.begin(), m_chain.end(), effectchain::GuitarSuite) != m_chain.end();
}

bool TrackEffectsPanel::supportsHarmonium() const
{
    return std::find(m_chain.begin(), m_chain.end(), effectchain::Harmonium) != m_chain.end();
}

bool TrackEffectsPanel::supportsDrumKit() const
{
    return std::find(m_chain.begin(), m_chain.end(), effectchain::DrumKit) != m_chain.end();
}

bool TrackEffectsPanel::supportsPiano() const
{
    return std::find(m_chain.begin(), m_chain.end(), effectchain::Piano) != m_chain.end();
}

bool TrackEffectsPanel::supportsChords() const
{
    return std::find(m_chain.begin(), m_chain.end(), effectchain::Chords) != m_chain.end();
}

bool TrackEffectsPanel::supportsElectroDrums() const
{
    return std::find(m_chain.begin(), m_chain.end(), effectchain::ElectroDrums) != m_chain.end();
}

void TrackEffectsPanel::setSelectedEffect(std::string_view key)
{
    m_selectedEffect = validateSelectedEffect(key, m_chain);
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
}

void TrackEffectsPanel::setChain(std::vector<std::string> chain)
{
    m_chain = std::move(chain);
    m_selectedEffect = validateSelectedEffect(m_selectedEffect, m_chain);
}

std::string TrackEffectsPanel::currentRecordModeDescription() const
{
    // The master track (G9) records nothing: say what its effects do instead.
    if (m_trackType == "Master") return "On the whole mix, before the master fader";
    return recordModeDescription(m_recordDry);
}
