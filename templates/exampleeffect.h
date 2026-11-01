#pragma once
#include "processor/processor.h"
#include "processor/smoother.h"

// One line: what this effect does.
class ExampleEffect final : public Processor {
public:
    ExampleEffect();

    std::string typeId() const override { return "exampleeffect"; }

    void prepare(double sampleRate, int maxFrames) override;    // not realtime: allocate here
    void reset() noexcept override;                             // clear tails, filters and delay lines
    void process(ProcessContext& context) noexcept override;    // realtime: see the five rules in the roadmap

    Parameter& amount() { return m_amount; }

private:
    Parameter& m_amount;
    Smoother m_amountSmoother;
};
