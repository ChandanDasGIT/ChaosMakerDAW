#include "fft.h"

#include <cmath>
#include <utility>

Fft::Fft(int size)
{
    while (m_size < size && m_size < (1 << 24)) m_size <<= 1;
    constexpr double twoPi = 6.283185307179586476925;
    m_twiddles.resize(std::size_t(m_size / 2));
    for (int k = 0; k < m_size / 2; ++k)
        m_twiddles[std::size_t(k)] = std::polar(1.0, -twoPi * k / m_size);
    int bits = 0;
    while ((1 << bits) < m_size) ++bits;
    m_reversed.resize(std::size_t(m_size));
    for (int i = 0; i < m_size; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        m_reversed[std::size_t(i)] = r;
    }
}

void Fft::forward(std::complex<double>* data) const noexcept
{
    for (int i = 0; i < m_size; ++i) {
        const int r = m_reversed[std::size_t(i)];
        if (r > i) std::swap(data[i], data[r]);
    }
    // Butterflies: combine pairs of half-size transforms, doubling the length each pass.
    for (int length = 2; length <= m_size; length <<= 1) {
        const int half = length / 2;
        const int stride = m_size / length;
        for (int start = 0; start < m_size; start += length) {
            for (int k = 0; k < half; ++k) {
                const auto t = m_twiddles[std::size_t(k * stride)] * data[start + k + half];
                data[start + k + half] = data[start + k] - t;
                data[start + k] += t;
            }
        }
    }
}
