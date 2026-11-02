#pragma once
#include "processor/processor.h"
#include "processor/smoother.h"
#include "dsp/partitionedconvolver.h"
#include "audiofile/impulsewav.h"
#include <array>
class Cabinet final : public Processor {
public:
    explicit Cabinet(CabinetImpulse impulse = {});
    std::string typeId() const override { return "pedal.cabinet"; }
    void prepare(double sampleRate,int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    const CabinetImpulse& impulseData() const noexcept { return impulse; }
    bool loaded() const noexcept { return !impulse.channels[0].empty(); }
private:
    CabinetImpulse impulse;
    std::array<PartitionedConvolver,2> convolvers;
    std::array<Parameter*,2> controls;
    std::array<Smoother,2> smoothers;
};
