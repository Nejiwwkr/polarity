#include "Fft.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace polarity::dsp
{

namespace
{
constexpr double kPi = 3.14159265358979323846264338327950288;

bool isPowerOfTwo (std::size_t n) noexcept
{
    return n != 0 && (n & (n - 1)) == 0;
}
} // namespace

void fftInPlace (std::vector<std::complex<double>>& data, bool inverse)
{
    const std::size_t n = data.size();

    if (! isPowerOfTwo (n))
        throw std::invalid_argument ("FFT size must be a power of two");

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;

        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
            std::swap (data[i], data[j]);
    }

    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const double angle = (inverse ? 2.0 : -2.0) * kPi
                             / static_cast<double> (length);
        const std::complex<double> step (std::cos (angle), std::sin (angle));

        for (std::size_t start = 0; start < n; start += length)
        {
            std::complex<double> w (1.0, 0.0);
            const std::size_t half = length / 2;

            for (std::size_t offset = 0; offset < half; ++offset)
            {
                const auto even = data[start + offset];
                const auto odd = data[start + offset + half] * w;

                data[start + offset] = even + odd;
                data[start + offset + half] = even - odd;
                w *= step;
            }
        }
    }

    if (inverse)
    {
        const double scale = 1.0 / static_cast<double> (n);

        for (auto& value : data)
            value *= scale;
    }
}

} // namespace polarity::dsp
