#include "guitarsuiteeditor.h"

#include "instruments/guitar/audiofile/impulsewav.h"
#include "instruments/guitar/plugins/cabinet/cabinet.h"
#include "instruments/guitar/plugins/guitarsuite/guitarsuite.h"
#include "instruments/guitar/plugins/guitarsuite/pedalregistry.h"
#include "instruments/guitar/plugins/tuner/tuner.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

GuitarSuiteEditor::GuitarSuiteEditor() = default;

std::shared_ptr<PedalSlot> GuitarSuiteEditor::slotAt(int index) const
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_slots.size()) {
        return nullptr;
    }
    return m_slots[static_cast<std::size_t>(index)];
}

std::shared_ptr<PedalSlot> GuitarSuiteEditor::findSlot(int pedalId) const
{
    const auto it = std::find_if(m_slots.begin(), m_slots.end(), [pedalId](const auto& slot) {
        return slot && slot->id == pedalId;
    });
    return it == m_slots.end() ? nullptr : *it;
}

int GuitarSuiteEditor::indexOfSlot(int pedalId) const
{
    const auto it = std::find_if(m_slots.begin(), m_slots.end(), [pedalId](const auto& slot) {
        return slot && slot->id == pedalId;
    });
    return it == m_slots.end() ? -1 : static_cast<int>(std::distance(m_slots.begin(), it));
}

bool GuitarSuiteEditor::addPedal(const std::string& type)
{
    if (m_busy) return false;
    auto processor = PedalRegistry::create(type);
    if (!processor) {
        return false;
    }
    auto slot = std::make_shared<PedalSlot>();
    slot->id = m_nextPedalId++;
    slot->type = processor->typeId();
    slot->processor = std::move(processor);

    m_slots.push_back(std::move(slot));
    publishChain();
    m_lastError.clear();
    return true;
}

bool GuitarSuiteEditor::removePedal(int pedalId)
{
    if (m_busy) return false;
    const auto it = std::find_if(m_slots.begin(), m_slots.end(), [pedalId](const auto& slot) {
        return slot && slot->id == pedalId;
    });
    if (it == m_slots.end()) return false;

    m_slots.erase(it);
    publishChain();
    return true;
}

bool GuitarSuiteEditor::movePedal(int from, int to)
{
    if (m_busy) return false;
    const int numSlots = count();
    if (from < 0 || to < 0 || from >= numSlots || to >= numSlots || from == to) {
        return false;
    }

    auto slot = std::move(m_slots[static_cast<std::size_t>(from)]);
    m_slots.erase(m_slots.begin() + from);
    m_slots.insert(m_slots.begin() + to, std::move(slot));
    publishChain();
    return true;
}

bool GuitarSuiteEditor::setBypassed(int pedalId, bool bypassed)
{
    const auto slot = findSlot(pedalId);
    if (!slot) return false;

    slot->bypassed.store(bypassed, std::memory_order_relaxed);
    for (const auto& copy : m_playbackSlots) {
        if (copy.source == slot && copy.mirror) {
            copy.mirror->bypassed.store(bypassed, std::memory_order_relaxed);
        }
    }
    return true;
}

bool GuitarSuiteEditor::setParameter(int pedalId, const std::string& parameterId, double value)
{
    if (!std::isfinite(value)) return false;
    const auto slot = findSlot(pedalId);
    if (!slot || !slot->processor) return false;

    for (const auto& parameter : slot->processor->parameters()) {
        if (parameter && parameter->id == parameterId) {
            parameter->set(static_cast<float>(value));
            for (const auto& copy : m_playbackSlots) {
                if (copy.source == slot && copy.mirror && copy.mirror->processor) {
                    for (const auto& mirrorParam : copy.mirror->processor->parameters()) {
                        if (mirrorParam && mirrorParam->id == parameter->id) {
                            mirrorParam->set(parameter->get());
                        }
                    }
                }
            }
            return true;
        }
    }
    return false;
}

void GuitarSuiteEditor::setInputTrimDb(double db)
{
    if (!std::isfinite(db)) return;
    m_inputTrimDb = std::clamp(db, -24.0, 24.0);
}

void GuitarSuiteEditor::configure(double sampleRate, int maxFrames)
{
    if (sampleRate > 1000.0 && std::isfinite(sampleRate)) {
        m_sampleRate = sampleRate;
    }
    m_maxFrames = std::max(1, maxFrames);
    publishChain();
}

void GuitarSuiteEditor::setPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher)
{
    m_publisher = std::move(publisher);
    publishChain();
}

void GuitarSuiteEditor::setPlaybackPublisher(std::function<void(std::shared_ptr<GuitarSuite>)> publisher)
{
    m_playbackPublisher = std::move(publisher);
    publishChain();
}

void GuitarSuiteEditor::publishChain()
{
    if (m_publisher) {
        auto chain = std::make_shared<GuitarSuite>(m_slots);
        chain->prepare(m_sampleRate, m_maxFrames);
        m_publisher(std::move(chain));
    }

    if (!m_playbackPublisher) return;

    std::vector<PlaybackSlot> next;
    std::vector<std::shared_ptr<PedalSlot>> playbackSlots;

    for (const auto& source : m_slots) {
        const auto existing = std::find_if(m_playbackSlots.begin(), m_playbackSlots.end(), [&](const auto& p) {
            return p.source == source;
        });

        std::shared_ptr<PedalSlot> mirror;
        if (existing != m_playbackSlots.end()) {
            mirror = existing->mirror;
        } else {
            mirror = std::make_shared<PedalSlot>();
            mirror->id = source->id;
            mirror->type = source->type;
            mirror->resourcePath = source->resourcePath;

            if (auto cabinet = std::dynamic_pointer_cast<Cabinet>(source->processor)) {
                mirror->processor = std::make_shared<Cabinet>(cabinet->impulseData());
            } else {
                mirror->processor = PedalRegistry::create(source->type);
            }

            if (mirror->processor && source->processor) {
                for (const auto& parameter : source->processor->parameters()) {
                    for (const auto& p : mirror->processor->parameters()) {
                        if (p->id == parameter->id) {
                            p->set(parameter->get());
                        }
                    }
                }
            }
            mirror->bypassed.store(source->bypassed.load(std::memory_order_relaxed));
        }

        next.push_back({source, mirror});
        playbackSlots.push_back(mirror);
    }

    m_playbackSlots = std::move(next);
    auto chain = std::make_shared<GuitarSuite>(std::move(playbackSlots));
    chain->prepare(m_sampleRate, m_maxFrames);
    m_playbackPublisher(std::move(chain));
}

TunerReading GuitarSuiteEditor::tunerReading(int pedalId) const
{
    const auto slot = findSlot(pedalId);
    if (!slot) return {};
    const auto tuner = std::dynamic_pointer_cast<Tuner>(slot->processor);
    if (!tuner) return {};

    return TunerState::evaluate(*tuner, slot->bypassed.load(std::memory_order_relaxed));
}

std::vector<std::string> GuitarSuiteEditor::factoryPresetNames()
{
    // Djent, MetalSolo, RockChords and RockSolo were fitted to reference recordings (plan 6.3).
    return {"Djent", "MetalSolo", "RockChords", "RockSolo", "Jazz"};
}

std::vector<std::string> GuitarSuiteEditor::availablePedals()
{
    return PedalRegistry::availablePedals();
}

bool GuitarSuiteEditor::savePreset()
{
    return savePresetTo(m_presetFilePath);
}

bool GuitarSuiteEditor::savePresetTo(const std::string& filePath)
{
    if (m_busy) return false;
    m_presetFilePath = filePath;
    auto preset = GuitarSuitePreset::extractPresetData(m_slots, m_inputTrimDb);
    if (!GuitarSuitePreset::saveToFile(filePath, preset, &m_lastError)) {
        return false;
    }
    m_lastError.clear();
    return true;
}

bool GuitarSuiteEditor::loadPreset()
{
    return loadPresetFrom(m_presetFilePath);
}

bool GuitarSuiteEditor::loadPresetFrom(const std::string& filePath)
{
    if (m_busy) return false;
    m_presetFilePath = filePath;

    std::error_code ec;
    auto size = fs::file_size(filePath, ec);
    if (ec) {
        m_lastError = "GuitarSuite preset not found: " + filePath;
        return false;
    }
    if (size > GuitarSuitePreset::MaxPresetFileSize) {
        m_lastError = "GuitarSuite preset is too large.";
        return false;
    }

    std::string baseDir = fs::path(filePath).parent_path().string();
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        m_lastError = "GuitarSuite preset not found: " + filePath;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    auto candidate = preparePresetData(buffer.str(), baseDir, false);
    if (!candidate.error.empty()) {
        m_lastError = candidate.error;
        return false;
    }

    if (candidate.needsAsync) {
        prepareSlotResources(candidate.candidateSlots, m_sampleRate, m_maxFrames);
    }
    commitPreset(std::move(candidate.candidateSlots), candidate.nextId, candidate.trim);
    return true;
}

GuitarSuiteEditor::PresetLoadCandidate GuitarSuiteEditor::preparePresetData(
    std::string_view jsonString,
    const std::string& directory,
    bool keepCalibrationAndCabinet)
{
    PresetLoadCandidate result;
    auto parsed = GuitarSuitePreset::parse(jsonString, directory, &result.error);
    if (!parsed) {
        return result;
    }

    const double trim = keepCalibrationAndCabinet ? m_inputTrimDb : parsed->inputTrimDb;
    result.trim = trim;
    int nextId = m_nextPedalId;

    if (!GuitarSuitePreset::instantiateSlots(
            *parsed,
            nextId,
            m_slots,
            m_sampleRate,
            keepCalibrationAndCabinet,
            result.candidateSlots,
            &result.error)) {
        return result;
    }

    result.nextId = nextId;
    result.needsAsync = std::any_of(result.candidateSlots.begin(), result.candidateSlots.end(), [](const auto& slot) {
        return !slot->resourcePath.empty() && slot->preparedSampleRate == 0.0;
    });

    return result;
}

void GuitarSuiteEditor::prepareSlotResources(
    std::vector<std::shared_ptr<PedalSlot>>& slots,
    double rate,
    int frames)
{
    for (auto& slot : slots) {
        if (!slot) continue;
        if (slot->preparedSampleRate != 0.0) continue;

        if (!slot->resourcePath.empty()) {
            auto processor = std::make_shared<Cabinet>(loadImpulseWav(slot->resourcePath, rate));
            if (slot->processor) {
                for (const auto& value : slot->processor->parameters()) {
                    for (const auto& param : processor->parameters()) {
                        if (param->id == value->id) {
                            param->set(value->get());
                        }
                    }
                }
            }
            slot->processor = std::move(processor);
        }

        if (slot->processor) {
            slot->processor->prepare(rate, frames);
            slot->preparedSampleRate = rate;
            slot->preparedMaxFrames = frames;
        }
    }
}

void GuitarSuiteEditor::commitPreset(
    std::vector<std::shared_ptr<PedalSlot>> candidateSlots,
    int nextId,
    double trim)
{
    m_slots = std::move(candidateSlots);
    m_nextPedalId = nextId;
    setInputTrimDb(trim);
    publishChain();
    m_lastError.clear();
}

bool GuitarSuiteEditor::clearCabinetIr(int pedalId)
{
    if (m_busy) return false;
    const auto old = findSlot(pedalId);
    if (!old || old->type != "pedal.cabinet") return false;

    auto slot = std::make_shared<PedalSlot>();
    slot->id = old->id;
    slot->type = old->type;
    slot->processor = std::make_shared<Cabinet>();

    if (old->processor) {
        for (const auto& value : old->processor->parameters()) {
            for (const auto& param : slot->processor->parameters()) {
                if (param->id == value->id) {
                    param->set(value->get());
                }
            }
        }
    }

    slot->bypassed.store(old->bypassed.load(std::memory_order_relaxed));
    const auto it = std::find(m_slots.begin(), m_slots.end(), old);
    if (it != m_slots.end()) {
        *it = std::move(slot);
    }
    publishChain();
    m_lastError.clear();
    return true;
}

bool GuitarSuiteEditor::replaceCabinetSlot(
    int pedalId,
    const std::string& irPath,
    std::shared_ptr<Processor> cabinetProcessor,
    double rate,
    int frames)
{
    const auto old = findSlot(pedalId);
    if (!old || old->type != "pedal.cabinet") return false;

    auto next = std::make_shared<PedalSlot>();
    next->id = old->id;
    next->type = old->type;
    next->resourcePath = irPath;
    next->processor = std::move(cabinetProcessor);

    if (old->processor && next->processor) {
        for (const auto& value : old->processor->parameters()) {
            for (const auto& param : next->processor->parameters()) {
                if (param->id == value->id) {
                    param->set(value->get());
                }
            }
        }
    }

    next->bypassed.store(old->bypassed.load(std::memory_order_relaxed));
    next->preparedSampleRate = rate;
    next->preparedMaxFrames = frames;

    const auto it = std::find(m_slots.begin(), m_slots.end(), old);
    if (it != m_slots.end()) {
        *it = std::move(next);
    }
    publishChain();
    return true;
}

std::string GuitarSuiteEditor::sessionData() const
{
    auto preset = GuitarSuitePreset::extractPresetData(m_slots, m_inputTrimDb);
    return GuitarSuitePreset::serialize(preset, "");
}

bool GuitarSuiteEditor::restoreSessionData(std::string_view bytes)
{
    if (m_busy) return false;
    auto candidate = preparePresetData(bytes, "", false);
    if (!candidate.error.empty()) {
        return false;
    }
    if (candidate.needsAsync) {
        prepareSlotResources(candidate.candidateSlots, m_sampleRate, m_maxFrames);
    }
    commitPreset(std::move(candidate.candidateSlots), candidate.nextId, candidate.trim);
    return true;
}
