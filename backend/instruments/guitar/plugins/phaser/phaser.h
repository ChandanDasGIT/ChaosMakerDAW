#pragma once
#include "instruments/guitar/dsp/pedalcontrols.h"

class Phaser final : public Processor {
public:
    Phaser();
    std::string typeId() const override { return "pedal.phaser"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
private:
    std::array<Parameter*, 7> controls;
    std::array<Smoother, 5> smoothers;
    std::array<std::array<float, 6>, 2> stageState{};
    std::array<float, 2> feedback{};
    Smoother sweepRate;
    bool initialiseRate = true;
    double rate = 48000, phase = 0;
};
