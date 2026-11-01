#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <algorithm>

// Fixed-size FFT tables are prepared before audio starts; transform allocates nothing.
template<std::size_t N> class Radix2FFT {
    static_assert(N>1 && std::has_single_bit(N));
public:
    using Spectrum=std::array<std::complex<float>,N>;
    void prepare() noexcept {
        for(std::size_t i=0;i<N;++i) {
            std::size_t value=i,reversed=0;
            for(int bit=0;bit<std::countr_zero(N);++bit) { reversed=(reversed<<1)|(value&1);value>>=1; }
            reverse[i]=reversed;
        }
        for(std::size_t i=0;i<N/2;++i) {
            const double angle=-6.283185307179586*double(i)/N;
            roots[i]={float(std::cos(angle)),float(std::sin(angle))};
        }
    }
    void transform(Spectrum& data,bool inverse=false) const noexcept {
        for(std::size_t i=0;i<N;++i)if(i<reverse[i])std::swap(data[i],data[reverse[i]]);
        for(std::size_t length=2;length<=N;length*=2)
            for(std::size_t start=0;start<N;start+=length)
                for(std::size_t j=0;j<length/2;++j) {
                    const auto root=inverse?std::conj(roots[j*N/length]):roots[j*N/length];
                    const auto a=data[start+j],b=data[start+j+length/2]*root;
                    data[start+j]=a+b;data[start+j+length/2]=a-b;
                }
        if(inverse)for(auto& value:data)value/=float(N);
    }
private:
    std::array<std::size_t,N> reverse{};
    std::array<std::complex<float>,N/2> roots{};
};
