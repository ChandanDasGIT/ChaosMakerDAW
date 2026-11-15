#include "biquad.h"

#include <cmath>
#include <complex>

namespace biquad {

double magnitude(const Coefficients& c, double frequencyHz, double sampleRate) noexcept
{
    constexpr double twoPi = 6.283185307179586476925;
    // Evaluate H(z) on the unit circle: z^-1 = e^(-jw).
    const double w = twoPi * frequencyHz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> numerator = c.b0 + c.b1 * z1 + c.b2 * z2;
    const std::complex<double> denominator = 1.0 + c.a1 * z1 + c.a2 * z2;
    const double below = std::abs(denominator);
    return below > 0.0 ? std::abs(numerator) / below : 0.0;
}

} // namespace biquad
