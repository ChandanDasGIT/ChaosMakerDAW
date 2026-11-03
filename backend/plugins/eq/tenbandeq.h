#pragma once

#include "processor/processor.h"

#include <array>
#include <string>

struct BandDescriptor {
    int index = 0;
    float frequencyHz = 0.0f;
    std::string shortLabel;
    std::string fullLabel;
    float minGainDb = -12.0f;
    float maxGainDb = 12.0f;
    float defaultGainDb = 0.0f;
    float stepSizeDb = 0.5f;
};

// Ten fixed-frequency peaking filters for track tone shaping.
class TenBandEq final : public Processor {
public:
    static constexpr int BandCount = 10;
    static constexpr float MinGainDb = -12.0f;
    static constexpr float MaxGainDb = 12.0f;
    static constexpr float DefaultGainDb = 0.0f;
    static constexpr float StepSizeDb = 0.5f;

    TenBandEq();
    std::string typeId() const override { return "eq.tenband"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    static constexpr const std::array<float, 10>& frequencies() noexcept { return Frequencies; }
    static const std::array<BandDescriptor, 10>& bandDescriptors();
    static const BandDescriptor& bandDescriptor(int index);
    static std::string formatGain(float gainDb);
    static bool isValidGain(float gainDb) noexcept;
    static float clampGain(float gainDb) noexcept;

    void setBandGain(int band, float gainDb) noexcept;
    float bandGain(int band) const noexcept;

private:
    struct Coefficients { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
    struct State { float z1 = 0.0f, z2 = 0.0f; };
    void updateBand(int band, float gainDb) noexcept;

    static constexpr std::array<float, 10> Frequencies{
        31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };
    std::array<Parameter*, 10> m_gains{};
    std::array<Coefficients, 10> m_coefficients{};
    std::array<std::array<State, 2>, 10> m_state{};
    std::array<float, 10> m_appliedGain{};
    double m_sampleRate = 48000.0;
};
