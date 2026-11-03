#pragma once

#include "instruments/guitar/plugins/guitarsuite/pedalslot.h"
#include "instruments/guitar/plugins/tuner/tunerstate.h"
#include "instruments/guitar/presets/guitarsuitepreset.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class GuitarSuite;

class GuitarSuiteEditor {
public:
    GuitarSuiteEditor();
    ~GuitarSuiteEditor() = default;

    // Slots & pedals
    const std::vector<std::shared_ptr<PedalSlot>>& pedalSlots() const { return m_slots; }
    int count() const { return static_cast<int>(m_slots.size()); }
    std::shared_ptr<PedalSlot> slotAt(int index) const;
    std::shared_ptr<PedalSlot> findSlot(int pedalId) const;
    int indexOfSlot(int pedalId) const;

    bool addPedal(const std::string& type);
    bool removePedal(int pedalId);
    bool movePedal(int from, int to);
    bool setBypassed(int pedalId, bool bypassed);
    bool setParameter(int pedalId, const std::string& parameterId, double value);

    // Input trim [-24, 24] dB
    double inputTrimDb() const { return m_inputTrimDb; }
    void setInputTrimDb(double db);

    // Audio configuration & publication
    void configure(double sampleRate, int maxFrames);
    double sampleRate() const { return m_sampleRate; }
    int maxFrames() const { return m_maxFrames; }
    void setPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher);
    void setPlaybackPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher);
    void publishChain();

    // Tuner
    TunerReading tunerReading(int pedalId) const;

    // Factory presets & available pedals
    static std::vector<std::string> factoryPresetNames();
    static std::vector<std::string> availablePedals();

    // Presets & persistence
    const std::string& presetFilePath() const { return m_presetFilePath; }
    void setPresetFilePath(std::string path) { m_presetFilePath = std::move(path); }
    const std::string& lastError() const { return m_lastError; }
    void setError(std::string message) { m_lastError = std::move(message); }
    bool busy() const { return m_busy; }
    void setBusy(bool busy) { m_busy = busy; }

    bool savePreset();
    bool savePresetTo(const std::string& filePath);
    bool loadPreset();
    bool loadPresetFrom(const std::string& filePath);

    // Parse and prepare candidate preset. If async loading is needed, candidateSlots is returned
    // and returns true with needsAsync = true.
    struct PresetLoadCandidate {
        std::vector<std::shared_ptr<PedalSlot>> candidateSlots;
        int nextId = 1;
        double trim = 0.0;
        bool needsAsync = false;
        std::string error;
    };
    PresetLoadCandidate preparePresetData(
        std::string_view jsonString,
        const std::string& directory,
        bool keepCalibrationAndCabinet = false);

    void commitPreset(std::vector<std::shared_ptr<PedalSlot>> candidateSlots, int nextId, double trim);

    // Worker thread resource preparation for IR loading
    static void prepareSlotResources(std::vector<std::shared_ptr<PedalSlot>>& slots, double rate, int frames);

    // Cabinet IR manipulation
    bool clearCabinetIr(int pedalId);
    bool replaceCabinetSlot(
        int pedalId,
        const std::string& irPath,
        std::shared_ptr<Processor> cabinetProcessor,
        double rate,
        int frames);

    // Session serialization
    std::string sessionData() const;
    bool restoreSessionData(std::string_view bytes);

private:
    std::vector<std::shared_ptr<PedalSlot>> m_slots;
    std::function<void(std::shared_ptr<GuitarSuite>)> m_publisher;
    std::function<void(std::shared_ptr<GuitarSuite>)> m_playbackPublisher;
    struct PlaybackSlot {
        std::shared_ptr<PedalSlot> source;
        std::shared_ptr<PedalSlot> mirror;
    };
    std::vector<PlaybackSlot> m_playbackSlots;

    std::string m_presetFilePath;
    std::string m_lastError;
    int m_nextPedalId = 1;
    double m_inputTrimDb = 0.0;
    double m_sampleRate = 48000.0;
    int m_maxFrames = 128;
    bool m_busy = false;
};
