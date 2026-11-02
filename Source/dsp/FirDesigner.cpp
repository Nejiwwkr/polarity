#include "FirDesigner.h"

#include "Fft.h"

#include <algorithm>
#include <complex>
#include <stdexcept>

namespace polarity::dsp
{

namespace
{
void validate (double sampleRate, int numTaps)
{
    if (sampleRate <= 0.0)
        throw std::invalid_argument ("sample rate must be positive");

    if (numTaps < 8 || (numTaps & 1) != 0
        || (numTaps & (numTaps - 1)) != 0)
        throw std::invalid_argument ("numTaps must be an even power of two >= 8");
}

FirKernel finishKernel (std::vector<std::complex<double>> spectrum, int numTaps)
{
    fftInPlace (spectrum, true);

    FirKernel kernel;
    kernel.taps.resize (static_cast<std::size_t> (numTaps));

    for (int i = 0; i < numTaps; ++i)
        kernel.taps[static_cast<std::size_t> (i)] = spectrum[static_cast<std::size_t> (i)].real();

    // Circular shift by N/2 makes the zero-phase kernel causal and gives it
    // an exact N/2-sample delay (for even N this is a half-array swap).
    std::rotate (kernel.taps.begin(),
                 kernel.taps.begin() + numTaps / 2,
                 kernel.taps.end());

    return kernel;
}
} // namespace

FirKernel designKernel (const CurveModel& curve,
                        double sampleRate,
                        int numTaps,
                        int smoothingRadiusBins)
{
    validate (sampleRate, numTaps);

    const int half = numTaps / 2;
    std::vector<double> gains (static_cast<std::size_t> (half + 1));

    for (int k = 0; k <= half; ++k)
    {
        const double freq = static_cast<double> (k) * sampleRate
                            / static_cast<double> (numTaps);
        gains[static_cast<std::size_t> (k)] = 1.0 - curve.amountAt (freq);
    }

    if (smoothingRadiusBins > 0)
    {
        std::vector<double> smoothed (gains.size());

        for (int k = 0; k <= half; ++k)
        {
            double sum = 0.0;
            int count = 0;

            for (int offset = -smoothingRadiusBins;
                 offset <= smoothingRadiusBins; ++offset)
            {
                const int index = std::clamp (k + offset, 0, half);
                sum += gains[static_cast<std::size_t> (index)];
                ++count;
            }

            smoothed[static_cast<std::size_t> (k)]
                = sum / static_cast<double> (count);
        }

        gains.swap (smoothed);
    }

    std::vector<std::complex<double>> spectrum (static_cast<std::size_t> (numTaps));

    for (int k = 0; k <= half; ++k)
    {
        const double gain = gains[static_cast<std::size_t> (k)];
        spectrum[static_cast<std::size_t> (k)] = { gain, 0.0 };

        if (k > 0 && k < half)
            spectrum[static_cast<std::size_t> (numTaps - k)] = { gain, 0.0 };
    }

    return finishKernel (std::move (spectrum), numTaps);
}

FirKernel designFlatKernel (int numTaps, double gain)
{
    validate (1.0, numTaps);

    std::vector<std::complex<double>> spectrum (static_cast<std::size_t> (numTaps),
                                                std::complex<double> (gain, 0.0));
    return finishKernel (std::move (spectrum), numTaps);
}

} // namespace polarity::dsp
