#pragma once

#include <complex>
#include <vector>

namespace polarity::dsp
{

/** In-place radix-2 FFT.  data.size() must be a power of two. */
void fftInPlace (std::vector<std::complex<double>>& data, bool inverse);

} // namespace polarity::dsp
