#pragma once

#include "CurveModel.h"

#include <vector>

namespace polarity::dsp
{

struct FirKernel
{
    std::vector<double> taps;

    int latencySamples() const noexcept
    {
        return static_cast<int> (taps.size() / 2);
    }
};

/**
    Frequency-samples H(f) = 1 - A(f) at numTaps evenly spaced bins, builds
    the conjugate-symmetric spectrum and inverse-FFTs it into a causal,
    linear-phase FIR.  No window is applied: that keeps A=0 and A=2 exact.
    The kernel delay is numTaps / 2 samples.
*/
FirKernel designKernel (const CurveModel& curve,
                        double sampleRate,
                        int numTaps,
                        int smoothingRadiusBins = 0);

/** Diagnostic helper: a flat kernel of the given gain (1 = delta, -1 = invert). */
FirKernel designFlatKernel (int numTaps, double gain);

} // namespace polarity::dsp
