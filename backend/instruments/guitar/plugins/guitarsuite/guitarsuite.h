#pragma once

#include "instruments/guitar/plugins/guitarsuite/pedalslot.h"
#include "processor/processor.h"
#include "processor/smoother.h"

#include <memory>
#include <vector>

// Realtime chain container. Rebuild it and publish a new EngineSnapshot when
// the UI changes topology; never mutate the audio thread's slot vector.
class GuitarSuite final : public Processor {
public:
    explicit GuitarSuite(std::vector<std::shared_ptr<PedalSlot>> slots);
    std::string typeId() const override { return "container.guitarsuite"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void analyseInput(ProcessContext& context) noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    struct RuntimeSlot {
        std::shared_ptr<PedalSlot> state;
        Smoother wet;
        std::vector<float> dry;
    };

    std::vector<std::shared_ptr<PedalSlot>> m_slots;
    std::vector<RuntimeSlot> m_runtime;
    int m_maxFrames = 0;
    int m_maxChannels = 2;
    double m_sampleRate = 48000.0;
};
