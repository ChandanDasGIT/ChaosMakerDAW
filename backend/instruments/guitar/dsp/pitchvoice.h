#pragma once
#include "dsp/fft.h"

#include <array>
#include <complex>

// Streaming phase-vocoder voice. 1024-sample wet-path delay, 128-sample hops.
// The dry signal is mixed separately by the pedal and remains immediate.
// Uses the shared backend FFT (its tables are built when the voice is created, off the audio
// thread; transforms never allocate).
class PitchVoice {
public:
    static constexpr int Window=1024,Hop=128,Bins=Window/2+1;
    void prepare() noexcept;
    void reset() noexcept;
    float process(float sample,float ratio) noexcept;
private:
    void render(float ratio) noexcept;
    Fft fft{Window};
    std::array<std::complex<double>,Window> spectrum{};
    std::array<float,Window> window{},input{};
    std::array<float,Window*2> output{};
    std::array<double,Bins> lastPhase{},synthesisPhase{};
    std::array<float,Bins> magnitude{},frequency{};
    int writePosition=0,readPosition=0,hopCount=0;
};
