#pragma once
#include "processor/processor.h"
#include "processor/smoother.h"
#include "instruments/guitar/dsp/partitionedconvolver.h"
#include "instruments/guitar/dsp/pedalfilter.h"
#include "instruments/guitar/audiofile/impulsewav.h"
#include <array>

// Speaker cabinet. With an impulse response (IR) loaded it convolves with it. Without one it
// uses a built-in model (plan 6.3 step 5), chosen by the Cab control:
//   0 None (dry, the old behaviour), 1 1x12 Open, 2 2x12, 3 4x12 Closed (default).
// Each model is a speaker/cabinet response built from EQ bands: low cut and resonance,
// presence peaks and the cone's steep roll-off. 4x12 Closed was fitted to a measured boutique
// 4x12 IR (1.7 dB rms from 60 Hz to 12 kHz). Mix and Level apply to both.
class Cabinet final : public Processor {
public:
    static constexpr int ModelCount = 4;
    static constexpr int MaxModelBands = 7;

    explicit Cabinet(CabinetImpulse impulse = {});
    std::string typeId() const override { return "pedal.cabinet"; }
    void prepare(double sampleRate,int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    const CabinetImpulse& impulseData() const noexcept { return impulse; }
    bool loaded() const noexcept { return !impulse.channels[0].empty(); }
private:
    void buildModel(int model) noexcept;
    CabinetImpulse impulse;
    std::array<PartitionedConvolver,2> convolvers;
    std::array<Parameter*,3> controls;   // mix, level, model
    std::array<Smoother,2> smoothers;    // mix, level (the model switches)
    std::array<PedalFilter, MaxModelBands> modelBands;
    int builtModel = -1;
    float modelTrim = 1;
    double rate = 48000;
};
