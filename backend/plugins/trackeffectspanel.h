#pragma once

#include <string>
#include <string_view>
#include <vector>

struct EffectEntry {
    std::string key;
    std::string title;
    std::string subtitle;
    bool available = true;
};

class TrackEffectsPanel {
public:
    TrackEffectsPanel() = default;

    static bool checkGuitarSuiteSupport(std::string_view trackType, bool hasLiveInput);
    static std::string validateSelectedEffect(std::string_view effectKey, bool supportsGuitarSuite);
    static std::string recordModeDescription(bool recordDry);
    static std::vector<EffectEntry> availableEntries(bool supportsGuitarSuite);

    int trackId() const { return m_trackId; }
    const std::string& trackName() const { return m_trackName; }
    const std::string& trackType() const { return m_trackType; }
    bool hasLiveInput() const { return m_hasLiveInput; }
    bool recordDry() const { return m_recordDry; }
    bool supportsGuitarSuite() const;

    const std::string& selectedEffect() const { return m_selectedEffect; }
    void setSelectedEffect(std::string_view key);

    void setTrackInfo(int trackId, std::string trackName, std::string trackType, bool hasLiveInput, bool recordDry);
    void setRecordDry(bool dry) { m_recordDry = dry; }

    std::vector<EffectEntry> effectEntries() const;
    std::string currentRecordModeDescription() const;

private:
    int m_trackId = -1;
    std::string m_trackName;
    std::string m_trackType;
    bool m_hasLiveInput = true;
    bool m_recordDry = false;
    std::string m_selectedEffect = "eq";
};
