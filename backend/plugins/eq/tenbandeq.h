#pragma once

#include "processor/processor.h"

#include <array>

// Ten fixed-frequency peaking filters for track tone shaping.
class TenBandEq final : public Processor {
public:
    TenBandEq();
    std::string typeId() const override { return "eq.tenband"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    static constexpr const std::array<float,10>& frequencies() noexcept { return Frequencies; }
    void setBandGain(int band, float gainDb) noexcept;

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
