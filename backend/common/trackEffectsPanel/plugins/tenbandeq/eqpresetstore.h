#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct EqPresetData {
    std::string format = "ChaosMakerEQ";
    int version = 1;
    std::array<float, 10> frequenciesHz{};
    std::array<float, 10> gainsDb{};
};

class EqPresetStore {
public:
    static constexpr std::size_t MaxPresetFileSize = 64 * 1024; // 64 KB

    static std::vector<std::string> factoryPresetNames();
    static std::optional<std::array<float, 10>> factoryPresetGains(int index);
    static std::optional<std::array<float, 10>> factoryPresetGains(std::string_view name);

    static std::optional<EqPresetData> parse(
        std::string_view jsonString,
        std::string* errorMessage = nullptr);

    static std::string serialize(const std::array<float, 10>& gainsDb);

    static bool loadFromFile(
        const std::string& filePath,
        std::array<float, 10>& outGains,
        std::string* errorMessage = nullptr);

    static bool saveToFile(
        const std::string& filePath,
        const std::array<float, 10>& gainsDb,
        std::string* errorMessage = nullptr);
};
