#pragma once

#include "instruments/guitar/dsp/oversampler2x.h"
#include "processor/processor.h"
#include "processor/smoother.h"

#include <array>

class Overdrive final : public Processor {
public:
    Overdrive();
    std::string typeId() const override { return "pedal.overdrive"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    struct ShapeState { float drive = 1.0f; };
    static float shape(float input, void* state) noexcept;

    Parameter& m_driveDb;
    Parameter& m_tone;
    Parameter& m_levelDb;
    std::array<Smoother, 3> m_smoothers;
    std::array<Oversampler2x, 2> m_oversamplers;
    std::array<float, 2> m_toneState{};
    double m_sampleRate = 48000.0;
};
