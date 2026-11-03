#include "eqpresetstore.h"

#include "eqpresets.h"
#include "tenbandeq.h"

#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::vector<std::string> EqPresetStore::factoryPresetNames()
{
    std::vector<std::string> names;
    names.reserve(eqpresets::factory.size());
    for (const auto& preset : eqpresets::factory) {
        names.emplace_back(preset.name);
    }
    return names;
}

std::optional<std::array<float, 10>> EqPresetStore::factoryPresetGains(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= eqpresets::factory.size()) {
        return std::nullopt;
    }
    return eqpresets::factory[static_cast<std::size_t>(index)].gains;
}

std::optional<std::array<float, 10>> EqPresetStore::factoryPresetGains(std::string_view name)
{
    for (const auto& preset : eqpresets::factory) {
        if (preset.name == name) {
            return preset.gains;
        }
    }
    return std::nullopt;
}

std::optional<EqPresetData> EqPresetStore::parse(
    std::string_view jsonString,
    std::string* errorMessage)
{
    auto setError = [errorMessage](const std::string& msg) {
        if (errorMessage) {
            *errorMessage = msg;
        }
    };

    auto j = nlohmann::json::parse(jsonString, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        setError("Invalid or unsupported EQ preset.");
        return std::nullopt;
    }

    if (!j.contains("format") || j["format"] != "ChaosMakerEQ" ||
        !j.contains("version") || j["version"] != 1 ||
        !j.contains("gainsDb") || !j["gainsDb"].is_array() || j["gainsDb"].size() != 10 ||
        !j.contains("frequenciesHz") || !j["frequenciesHz"].is_array() || j["frequenciesHz"].size() != 10) {
        setError("Invalid or unsupported EQ preset.");
        return std::nullopt;
    }

    EqPresetData data;
    const auto& expectedHz = TenBandEq::frequencies();

    for (std::size_t i = 0; i < 10; ++i) {
        const auto& gainVal = j["gainsDb"][i];
        const auto& hzVal = j["frequenciesHz"][i];

        if (!gainVal.is_number() || !hzVal.is_number()) {
            setError("EQ preset must contain ten matching bands within ±12 dB.");
            return std::nullopt;
        }

        const double gain = gainVal.get<double>();
        const double hz = hzVal.get<double>();

        if (!std::isfinite(gain) || gain < -12.0 || gain > 12.0 ||
            !std::isfinite(hz) || static_cast<float>(hz) != expectedHz[i]) {
            setError("EQ preset must contain ten matching bands within ±12 dB.");
            return std::nullopt;
        }

        data.gainsDb[i] = static_cast<float>(gain);
        data.frequenciesHz[i] = static_cast<float>(hz);
    }

    return data;
}

std::string EqPresetStore::serialize(const std::array<float, 10>& gainsDb)
{
    nlohmann::json j;
    j["format"] = "ChaosMakerEQ";
    j["version"] = 1;

    nlohmann::json frequencies = nlohmann::json::array();
    for (float hz : TenBandEq::frequencies()) {
        frequencies.push_back(hz);
    }
    j["frequenciesHz"] = frequencies;

    nlohmann::json gains = nlohmann::json::array();
    for (float gain : gainsDb) {
        gains.push_back(gain);
    }
    j["gainsDb"] = gains;

    return j.dump(2);
}

bool EqPresetStore::loadFromFile(
    const std::string& filePath,
    std::array<float, 10>& outGains,
    std::string* errorMessage)
{
    auto setError = [errorMessage](const std::string& msg) {
        if (errorMessage) {
            *errorMessage = msg;
        }
    };

    std::error_code ec;
    auto size = fs::file_size(filePath, ec);
    if (ec || size > MaxPresetFileSize) {
        setError("Could not read EQ preset.");
        return false;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        setError("Could not read EQ preset.");
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    auto parsed = parse(buffer.str(), errorMessage);
    if (!parsed) {
        return false;
    }

    outGains = parsed->gainsDb;
    return true;
}

bool EqPresetStore::saveToFile(
    const std::string& filePath,
    const std::array<float, 10>& gainsDb,
    std::string* errorMessage)
{
    auto setError = [errorMessage](const std::string& msg) {
        if (errorMessage) {
            *errorMessage = msg;
        }
    };

    try {
        fs::path p(filePath);
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path());
        }
    } catch (const std::exception&) {
        setError("Could not save EQ preset.");
        return false;
    }

    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        setError("Could not save EQ preset.");
        return false;
    }

    std::string content = serialize(gainsDb);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good()) {
        setError("Could not save EQ preset.");
        return false;
    }

    return true;
}
