#include "dsp/CurveModel.h"
#include "dsp/FirDesigner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace polarity::dsp;

namespace
{
constexpr double kPi = 3.14159265358979323846264338327950288;

std::vector<double> convolve (const std::vector<double>& x,
                              const std::vector<double>& h)
{
    std::vector<double> y (x.size(), 0.0);

    for (std::size_t n = 0; n < x.size(); ++n)
    {
        const std::size_t count = std::min (h.size(), n + 1);
        double sum = 0.0;

        for (std::size_t k = 0; k < count; ++k)
            sum += h[k] * x[n - k];

        y[n] = sum;
    }

    return y;
}

double maxDelayedError (const std::vector<double>& y,
                        const std::vector<double>& x,
                        int delay,
                        double sign)
{
    double error = 0.0;

    for (std::size_t i = 0; i + static_cast<std::size_t> (delay) < y.size(); ++i)
        error = std::max (error, std::abs (y[i + static_cast<std::size_t> (delay)]
                                           - sign * x[i]));

    return error;
}

double sineAttenuationDb (const FirKernel& kernel, double sampleRate, double freq,
                          double seconds = 0.5)
{
    const int count = static_cast<int> (sampleRate * seconds);
    std::vector<double> x (static_cast<std::size_t> (count));

    for (int i = 0; i < count; ++i)
        x[static_cast<std::size_t> (i)] = std::sin (2.0 * kPi * freq
                                                    * static_cast<double> (i) / sampleRate);

    const auto y = convolve (x, kernel.taps);
    double sum = 0.0;
    int samples = 0;

    for (int i = count / 2; i < count; ++i)
    {
        sum += y[static_cast<std::size_t> (i)] * y[static_cast<std::size_t> (i)];
        ++samples;
    }

    const double rms = std::sqrt (sum / static_cast<double> (samples));
    return 20.0 * std::log10 (std::max (rms * std::sqrt (2.0), 1.0e-15));
}

bool near (double actual, double expected, double tolerance)
{
    return std::abs (actual - expected) <= tolerance;
}
} // namespace

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int taps = 4096;
    constexpr int delay = taps / 2;

    bool ok = true;

    std::mt19937 rng (1);
    std::normal_distribution<double> noise (0.0, 0.1);
    std::vector<double> input (48000);

    for (auto& value : input)
        value = noise (rng);

    // 1) A = 0: exact identity, delayed by N/2.
    const auto identityKernel = designFlatKernel (taps, 1.0);
    const auto identityOutput = convolve (input, identityKernel.taps);
    const double identityError = maxDelayedError (identityOutput, input, delay, 1.0);
    std::printf ("identity       : max error %.3e\n", identityError);
    ok = ok && identityError < 1.0e-9;

    // 2) A = 2 flat: exact polarity inversion, delayed by N/2.
    const auto invertKernel = designFlatKernel (taps, -1.0);
    const auto invertOutput = convolve (input, invertKernel.taps);
    const double invertError = maxDelayedError (invertOutput, input, delay, -1.0);
    std::printf ("full invert    : max error %.3e\n", invertError);
    ok = ok && invertError < 1.0e-9;

    // 3) Curve evaluation at control points.
    CurveModel band ({ { 20.0, 0.0 },
                       { 100.0, 0.0 },
                       { 200.0, 1.0 },
                       { 2000.0, 1.0 },
                       { 4000.0, 0.0 },
                       { 20000.0, 0.0 } });

    const bool curveOk = near (band.amountAt (20.0), 0.0, 1.0e-12)
                      && near (band.amountAt (200.0), 1.0, 1.0e-9)
                      && near (band.amountAt (1000.0), 1.0, 1.0e-9)
                      && near (band.amountAt (20000.0), 0.0, 1.0e-12);
    std::printf ("curve control  : %s\n", curveOk ? "ok" : "FAILED");
    ok = ok && curveOk;

    // 4) Band A = 1: measure in-band null depth.
    const auto bandKernel = designKernel (band, sampleRate, taps);
    std::printf ("band null      :\n");

    for (const double freq : { 400.0, 800.0, 1200.0, 1600.0 })
    {
        const double attenuation = sineAttenuationDb (bandKernel, sampleRate, freq);
        std::printf ("  %7.0f Hz    : %8.1f dB\n", freq, attenuation);
        ok = ok && attenuation < -60.0;
    }

    std::printf ("latency        : %d samples (%.1f ms @ %.0f Hz)\n",
                 delay, 1000.0 * static_cast<double> (delay) / sampleRate, sampleRate);
    std::printf ("resolution     : %.2f Hz\n", sampleRate / static_cast<double> (taps));

    // Light cross-rate checks: flat kernels are rate independent, but this
    // catches wiring mistakes; the wide curve keeps 192 kHz honest.
    const CurveModel wideCurve ({ { 20.0, 0.0 },
                                  { 100.0, 0.0 },
                                  { 500.0, 1.0 },
                                  { 5000.0, 1.0 },
                                  { 10000.0, 0.0 },
                                  { 20000.0, 0.0 } });

    std::printf ("rate matrix    :\n");

    for (const double rate : { 44100.0, 96000.0, 192000.0 })
    {
        std::vector<double> shortInput (static_cast<std::size_t> (taps + 2000));

        for (auto& value : shortInput)
            value = noise (rng);

        const auto flatKernel = designFlatKernel (taps, 1.0);
        const auto flatOutput = convolve (shortInput, flatKernel.taps);
        const double flatError = maxDelayedError (flatOutput, shortInput, delay, 1.0);

        const auto invertKernel = designFlatKernel (taps, -1.0);
        const auto invertOutput = convolve (shortInput, invertKernel.taps);
        const double invertError = maxDelayedError (invertOutput, shortInput, delay, -1.0);

        const auto kernel = designKernel (wideCurve, rate, taps);
        const double attenuation = sineAttenuationDb (kernel, rate, 1000.0, 0.125);

        std::printf ("  %6.0f Hz   : flat %.1e  invert %.1e  null %6.1f dB  lat %.2f ms\n",
                     rate, flatError, invertError, attenuation,
                     1000.0 * static_cast<double> (delay) / rate);

        ok = ok && flatError < 1.0e-9
                && invertError < 1.0e-9
                && attenuation < -40.0;
    }

    std::printf ("DSP SELF-TEST  : %s\n", ok ? "PASS" : "FAIL");

    return ok ? 0 : 1;
}
