#pragma once

#include <complex>
#include <vector>

// A plain radix-2 FFT, written here so the project needs no FFT library.
// Construct once (allocates the tables), then transform as often as needed.
class Fft {
public:
    // size is rounded up to a power of two, at least 2.
    explicit Fft(int size);
    int size() const noexcept { return m_size; }

    // In place, data must hold size() values. No allocation.
    void forward(std::complex<double>* data) const noexcept;
    // The inverse transform, scaled by 1/size(): inverse(forward(x)) == x. No allocation.
    void inverse(std::complex<double>* data) const noexcept;

private:
    int m_size = 2;
    std::vector<std::complex<double>> m_twiddles;   // e^(-2 pi i k / size), k < size / 2
    std::vector<int> m_reversed;                     // bit-reversed index of each position
};
