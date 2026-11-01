#pragma once
#include <array>
#include <complex>
#include <vector>

// Zero added latency: a direct 128-tap head plus FFT-partitioned tail.
// Internal partitions are independent of host block size. Prepare off-thread.
class PartitionedConvolver {
public:
    void prepare(const std::vector<float>& impulse);
    void reset() noexcept;
    float process(float sample) noexcept;
private:
    static constexpr int P=128,N=256;
    using Spectrum=std::array<std::complex<float>,N>;
    void fft(Spectrum& data,bool inverse) noexcept;
    void completeBlock() noexcept;
    std::array<float,P> head{},history{},block{},tail{},overlap{};
    std::array<int,N> reverse{};
    std::array<std::complex<float>,N/2> roots{};
    std::vector<Spectrum> kernels,spectra;
    Spectrum work{},sum{};
    int position=0,headPosition=0,spectrumPosition=0;
};
