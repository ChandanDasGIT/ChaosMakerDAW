#pragma once
#include "radix2fft.h"

// Streaming phase-vocoder voice. 1024-sample wet-path delay, 128-sample hops.
// The dry signal is mixed separately by the pedal and remains immediate.
class PitchVoice {
public:
    static constexpr int Window=1024,Hop=128,Bins=Window/2+1;
    void prepare() noexcept;
    void reset() noexcept;
    float process(float sample,float ratio) noexcept;
private:
    void render(float ratio) noexcept;
    Radix2FFT<Window> fft;
    Radix2FFT<Window>::Spectrum spectrum{};
    std::array<float,Window> window{},input{};
    std::array<float,Window*2> output{};
    std::array<double,Bins> lastPhase{},synthesisPhase{};
    std::array<float,Bins> magnitude{},frequency{};
    int writePosition=0,readPosition=0,hopCount=0;
};
