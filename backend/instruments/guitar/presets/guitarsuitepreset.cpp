#include "guitarsuitepreset.h"

#include "instruments/guitar/plugins/guitarsuite/pedalregistry.h"
#include "instruments/guitar/plugins/cabinet/cabinet.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::optional<GuitarSuitePresetData> GuitarSuitePreset::parse(
    std::string_view jsonString,
    const std::string& baseDirectory,
    std::string* errorMessage)
{
    auto setError = [errorMessage](const std::string& msg) {
        if (errorMessage) {
            *errorMessage = msg;
        }
    };

    auto j = nlohmann::json::parse(jsonString, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        setError("Invalid or unsupported GuitarSuite preset.");
        return std::nullopt;
    }

    if (!j.contains("format") || j["format"] != "GuitarSuitePreset" ||
        !j.contains("version") || j["version"] != 1 ||
        !j.contains("pedals") || !j["pedals"].is_array()) {
        setError("Invalid or unsupported GuitarSuite preset.");
        return std::nullopt;
    }

    GuitarSuitePresetData data;
    data.format = "GuitarSuitePreset";
    data.version = 1;

    if (j.contains("name") && j["name"].is_string()) {
        data.name = j["name"].get<std::string>();
    }

    if (j.contains("inputTrimDb")) {
        if (!j["inputTrimDb"].is_number()) {
            setError("Invalid input trim in GuitarSuite preset.");
            return std::nullopt;
        }
        data.inputTrimDb = j["inputTrimDb"].get<double>();
    } else {
        data.inputTrimDb = 0.0;
    }

    if (!std::isfinite(data.inputTrimDb) || data.inputTrimDb < -24.0 || data.inputTrimDb > 24.0) {
        setError("Invalid input trim in GuitarSuite preset.");
        return std::nullopt;
    }

    for (const auto& item : j["pedals"]) {
        if (!item.is_object()) {
            setError("Invalid pedal entry in GuitarSuite preset.");
            return std::nullopt;
        }

        if (!item.contains("type") || !item["type"].is_string()) {
            setError("Invalid pedal entry in GuitarSuite preset.");
            return std::nullopt;
        }

        PedalPresetData pedal;
        pedal.type = item["type"].get<std::string>();

        if (item.contains("bypassed") && item["bypassed"].is_boolean()) {
            pedal.bypassed = item["bypassed"].get<bool>();
        }

        if (item.contains("irPath")) {
            if (!item["irPath"].is_string() && !item["irPath"].is_null()) {
                setError("Invalid cabinet IR reference.");
                return std::nullopt;
            }
            if (item["irPath"].is_string()) {
                std::string ir = item["irPath"].get<std::string>();
                if (!ir.empty()) {
                    if (!baseDirectory.empty()) {
                        fs::path basePath(baseDirectory);
                        fs::path irP(ir);
                        if (irP.is_relative()) {
                            pedal.irPath = fs::weakly_canonical(basePath / irP).string();
                        } else {
                            pedal.irPath = fs::weakly_canonical(irP).string();
                        }
                    } else {
                        pedal.irPath = ir;
                    }
                }
            }
        }

        if (item.contains("parameters") && item["parameters"].is_object()) {
            for (auto it = item["parameters"].begin(); it != item["parameters"].end(); ++it) {
                if (it.value().is_number()) {
                    pedal.parameters[it.key()] = it.value().get<float>();
                }
            }
        }

        data.pedals.push_back(std::move(pedal));
    }

    return data;
}

std::string GuitarSuitePreset::serialize(
    const GuitarSuitePresetData& preset,
    const std::string& baseDirectory)
{
    nlohmann::json j;
    j["format"] = preset.format.empty() ? "GuitarSuitePreset" : preset.format;
    j["version"] = preset.version > 0 ? preset.version : 1;
    j["inputTrimDb"] = preset.inputTrimDb;

    nlohmann::json pedalsArray = nlohmann::json::array();
    for (const auto& pedal : preset.pedals) {
        nlohmann::json p;
        p["type"] = pedal.type;
        p["bypassed"] = pedal.bypassed;

        nlohmann::json params = nlohmann::json::object();
        for (const auto& [key, val] : pedal.parameters) {
            params[key] = val;
        }
        p["parameters"] = params;

        if (!pedal.irPath.empty()) {
            if (!baseDirectory.empty()) {
                std::error_code ec;
                fs::path baseP(baseDirectory);
                fs::path irP(pedal.irPath);
                fs::path rel = fs::relative(irP, baseP, ec);
                p["irPath"] = ec ? pedal.irPath : rel.string();
            } else {
                p["irPath"] = pedal.irPath;
            }
        } else {
            p["irPath"] = "";
        }

        pedalsArray.push_back(std::move(p));
    }

    j["pedals"] = pedalsArray;
    if (!preset.name.empty()) {
        j["name"] = preset.name;
    }

    return j.dump(2);
}

bool GuitarSuitePreset::loadFromFile(
    const std::string& filePath,
    GuitarSuitePresetData& outData,
    std::string* errorMessage)
{
    auto setError = [errorMessage](const std::string& msg) {
        if (errorMessage) {
            *errorMessage = msg;
        }
    };

    std::error_code ec;
    auto size = fs::file_size(filePath, ec);
    if (ec) {
        setError("GuitarSuite preset not found: " + filePath);
        return false;
    }

    if (size > MaxPresetFileSize) {
        setError("GuitarSuite preset is too large.");
        return false;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        setError("GuitarSuite preset not found: " + filePath);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    std::string baseDir = fs::path(filePath).parent_path().string();
    auto parsed = parse(content, baseDir, errorMessage);
    if (!parsed) {
        return false;
    }

    outData = std::move(*parsed);
    return true;
}

bool GuitarSuitePreset::saveToFile(
    const std::string& filePath,
    const GuitarSuitePresetData& preset,
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
        setError("Could not create GuitarSuite preset folder.");
        return false;
    }

    std::string baseDir = fs::path(filePath).parent_path().string();
    std::string serialized = serialize(preset, baseDir);

    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        setError("Could not write GuitarSuite preset.");
        return false;
    }

    file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    if (!file.good()) {
        setError("Could not finish writing GuitarSuite preset.");
        return false;
    }

    return true;
}

bool GuitarSuitePreset::instantiateSlots(
    const GuitarSuitePresetData& preset,
    int& nextId,
    const std::vector<std::shared_ptr<PedalSlot>>& existingSlots,
    double sampleRate,
    bool keepCalibrationAndCabinet,
    std::vector<std::shared_ptr<PedalSlot>>& outSlots,
    std::string* errorMessage)
{
    auto setError = [errorMessage](const std::string& msg) {
        if (errorMessage) {
            *errorMessage = msg;
        }
    };

    std::vector<std::shared_ptr<PedalSlot>> candidate;

    for (const auto& pedal : preset.pedals) {
        auto processor = PedalRegistry::create(pedal.type);
        if (!processor) {
            setError("Unknown pedal type in GuitarSuite preset: " + pedal.type);
            return false;
        }

        auto slot = std::make_shared<PedalSlot>();
        slot->id = nextId++;
        slot->type = pedal.type;
        slot->processor = std::move(processor);
        slot->bypassed.store(pedal.bypassed, std::memory_order_relaxed);
        slot->resourcePath = pedal.irPath;

        if (keepCalibrationAndCabinet && pedal.type == "pedal.cabinet") {
            const auto existing = std::find_if(existingSlots.begin(), existingSlots.end(), [](const auto& entry) {
                return entry->type == "pedal.cabinet" && !entry->resourcePath.empty();
            });
            if (existing != existingSlots.end()) {
                const auto& current = *existing;
                slot->resourcePath = current->resourcePath;
                if (current->preparedSampleRate == sampleRate) {
                    slot->processor = current->processor;
                    slot->preparedSampleRate = current->preparedSampleRate;
                    slot->preparedMaxFrames = current->preparedMaxFrames;
                } else {
                    for (const auto& value : current->processor->parameters()) {
                        for (const auto& param : slot->processor->parameters()) {
                            if (value->id == param->id) {
                                param->set(value->get());
                            }
                        }
                    }
                }
                candidate.push_back(std::move(slot));
                continue;
            }
        }

        for (const auto& param : slot->processor->parameters()) {
            auto it = pedal.parameters.find(param->id);
            if (it != pedal.parameters.end() && std::isfinite(it->second)) {
                param->set(it->second);
            }
        }

        candidate.push_back(std::move(slot));
    }

    outSlots = std::move(candidate);
    return true;
}

GuitarSuitePresetData GuitarSuitePreset::extractPresetData(
    const std::vector<std::shared_ptr<PedalSlot>>& slots,
    double inputTrimDb,
    const std::string& name)
{
    GuitarSuitePresetData data;
    data.name = name;
    data.inputTrimDb = inputTrimDb;

    for (const auto& slot : slots) {
        PedalPresetData p;
        p.type = slot->type;
        p.bypassed = slot->bypassed.load(std::memory_order_relaxed);
        p.irPath = slot->resourcePath;

        if (slot->processor) {
            for (const auto& param : slot->processor->parameters()) {
                p.parameters[param->id] = param->get();
            }
        }

        data.pedals.push_back(std::move(p));
    }

    return data;
}
