#pragma once

#include "instruments/guitar/plugins/guitarsuite/pedalslot.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct PedalPresetData {
    std::string type;
    bool bypassed = false;
    std::unordered_map<std::string, float> parameters;
    std::string irPath;
};

struct GuitarSuitePresetData {
    std::string format = "GuitarSuitePreset";
    int version = 1;
    std::string name;
    double inputTrimDb = 0.0;
    std::vector<PedalPresetData> pedals;
};

class GuitarSuitePreset {
public:
    static constexpr std::size_t MaxPresetFileSize = 1024 * 1024; // 1 MB limit

    static std::optional<GuitarSuitePresetData> parse(
        std::string_view jsonString,
        const std::string& baseDirectory = {},
        std::string* errorMessage = nullptr);

    static std::string serialize(
        const GuitarSuitePresetData& preset,
        const std::string& baseDirectory = {});

    static bool loadFromFile(
        const std::string& filePath,
        GuitarSuitePresetData& outData,
        std::string* errorMessage = nullptr);

    static bool saveToFile(
        const std::string& filePath,
        const GuitarSuitePresetData& preset,
        std::string* errorMessage = nullptr);

    // Build slots from parsed preset data.
    // Handles cabinet reuse, parameter application, and ID numbering.
    static bool instantiateSlots(
        const GuitarSuitePresetData& preset,
        int& nextId,
        const std::vector<std::shared_ptr<PedalSlot>>& existingSlots,
        double sampleRate,
        bool keepCalibrationAndCabinet,
        std::vector<std::shared_ptr<PedalSlot>>& outSlots,
        std::string* errorMessage = nullptr);

    // Extract preset data from active slots and input trim
    static GuitarSuitePresetData extractPresetData(
        const std::vector<std::shared_ptr<PedalSlot>>& slots,
        double inputTrimDb,
        const std::string& name = {});
};
