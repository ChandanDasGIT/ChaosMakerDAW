#pragma once

#include "instruments/guitar/dsp/drivestage.h"
#include "instruments/guitar/dsp/oversampler2x.h"
#include "instruments/guitar/dsp/pedalfilter.h"
#include "processor/processor.h"
#include "processor/smoother.h"

#include <array>

// Tube-Screamer-style overdrive (reworked in plan 6.3). Only the part of the signal above
// ~720 Hz is driven into soft symmetric clipping and added back to the clean signal, which
// gives the classic mid hump and keeps the low end clean and tight. Tone is a treble shelf
// after a gentle top-end roll-off. Level is a pure output gain.
class Overdrive final : public Processor {
public:
    Overdrive();
    std::string typeId() const override { return "pedal.overdrive"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    static constexpr int RefreshFrames = 16;
    // Clip path state for one channel, at the 2x rate.
    struct ShapeState {
        float gain = 1.0f;
        float highpassCoefficient = 0.9f, previousInput = 0.0f, highpass = 0.0f;
        DriveStage stage;
    };
    static float shape(float input, void* state) noexcept;
    void refresh(float tone) noexcept;

    Parameter& m_driveDb;
    Parameter& m_tone;
    Parameter& m_levelDb;
    std::array<Smoother, 3> m_smoothers;
    std::array<Oversampler2x, 2> m_oversamplers;
    std::array<ShapeState, 2> m_shape;
    PedalFilter m_rolloff, m_toneShelf;
    float m_appliedTone = -1.0f;
    long long m_frameCounter = 0;
    double m_sampleRate = 48000.0;
};
