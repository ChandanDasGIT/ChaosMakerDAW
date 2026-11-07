#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

// View state of a track's FX panel: which effect of the track's chain is shown (plan 6.1).
// The chain itself (types in processing order) comes from the track; rules live in effectchain.
class TrackEffectsPanel {
public:
    TrackEffectsPanel() = default;

    // The key if it is on the chain, else the chain's first effect, else "" (empty chain).
    static std::string validateSelectedEffect(std::string_view effectKey, std::span<const std::string> chain);
    static std::string recordModeDescription(bool recordDry);

    int trackId() const { return m_trackId; }
    const std::string& trackName() const { return m_trackName; }
    const std::string& trackType() const { return m_trackType; }
    bool hasLiveInput() const { return m_hasLiveInput; }
    bool recordDry() const { return m_recordDry; }
    const std::vector<std::string>& chain() const { return m_chain; }
    // GuitarSuite's editor is shown only when GuitarSuite is on the chain.
    bool supportsGuitarSuite() const;
    // The harmonium's page likewise, on Harmonium tracks.
    bool supportsHarmonium() const;
    // The drum kit's page likewise, on Drums tracks.
    bool supportsDrumKit() const;

    const std::string& selectedEffect() const { return m_selectedEffect; }
    void setSelectedEffect(std::string_view key);

    void setTrackInfo(int trackId, std::string trackName, std::string trackType, bool hasLiveInput, bool recordDry);
    void setChain(std::vector<std::string> chain);
    void setRecordDry(bool dry) { m_recordDry = dry; }

    std::string currentRecordModeDescription() const;

private:
    int m_trackId = -1;
    std::string m_trackName;
    std::string m_trackType;
    bool m_hasLiveInput = true;
    bool m_recordDry = false;
    std::vector<std::string> m_chain;
    std::string m_selectedEffect;
};
