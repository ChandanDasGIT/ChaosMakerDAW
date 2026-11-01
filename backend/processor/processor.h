#pragma once
#include "core/audioview.h"
#include "midi/midievent.h"
#include "processor/parameter.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Everything a processor gets for one block.
struct ProcessContext {
    AudioView audio;                                      // processed in place
    MidiBuffer* midi = nullptr;
    double sampleRate = 48000.0;
    std::int64_t samplePosition = 0;
    double beat = 0.0;                                    // quarter notes from the song start
    double bpm = 120.0;
    double beatsPerBar = 4.0;                             // in quarter notes: 6/8 is 3.0
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    bool playing = false;
};

// Every effect and instrument is one of these.
class Processor {
public:
    virtual ~Processor() = default;

    virtual std::string typeId() const = 0;               // "eq", "reverb": used by save files and the registry

    // UI or worker thread, never at the same time as process(). Allocate here.
    virtual void prepare(double sampleRate, int maxFrames) = 0;

    // Audio thread: no allocation, no locks, no files.
    virtual void reset() noexcept = 0;
    virtual void process(ProcessContext& context) noexcept = 0;

    virtual bool analysisOnly() const noexcept { return false; }
    virtual void analyseInput(ProcessContext&) noexcept {}

    virtual int latencySamples() const noexcept { return 0; }

    std::span<const std::unique_ptr<Parameter>> parameters() const { return m_parameters; }

protected:
    Parameter& addParameter(std::string id, std::string name, float min, float max, float def, std::string unit = {}) {
        m_parameters.push_back(std::make_unique<Parameter>(std::move(id), std::move(name), min, max, def, std::move(unit)));
        return *m_parameters.back();
    }

private:
    std::vector<std::unique_ptr<Parameter>> m_parameters;
};
