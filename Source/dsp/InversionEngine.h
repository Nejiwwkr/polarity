#pragma once

#include "FirDesigner.h"

#include <array>
#include <atomic>
#include <vector>

namespace polarity::dsp
{

/**
    Real-time per-channel FIR convolution with click-free kernel replacement.

    The message thread publishes a new stereo kernel pair into one of four
    preallocated buffers; the audio thread picks it up at a block boundary and
    crossfades from the previous pair over ~40 ms.  No allocation, locking or
    FFT work happens on the audio thread.
*/
class InversionEngine
{
public:
    static constexpr int numKernelBuffers = 4;

    void prepare (double sampleRate, int numTaps, int numChannels);
    void reset();

    /** Non-real-time: install the initial pair before audio starts. */
    void setKernelsImmediate (const FirKernel& left, const FirKernel& right);

    /** Message thread: queue a pair; dropped if a fade/queue is already busy. */
    void setKernels (const FirKernel& left, const FirKernel& right);

    void process (float* const* channels, int numChannels, int numSamples);

    int getNumTaps() const noexcept { return numTaps; }
    int getLatencySamples() const noexcept { return numTaps / 2; }

private:
    struct ChannelState
    {
        std::vector<double> history;
        int writePos = 0;
    };

    struct KernelPair
    {
        std::vector<double> left;   // time-reversed taps
        std::vector<double> right;
    };

    static double dotProduct (const std::vector<double>& history,
                              int writePos,
                              const double* kernel) noexcept;

    void processChannel (ChannelState& state,
                         float* data,
                         int numSamples,
                         const double* currentKernel,
                         const double* previousKernel,
                         int fadeStart,
                         bool blockFading) noexcept;

    void consumePending();
    void lockState() noexcept;
    void unlockState() noexcept;

    std::vector<ChannelState> channelStates;
    std::array<KernelPair, numKernelBuffers> kernels;

    std::atomic<int> currentIndex { 0 };
    std::atomic<int> previousIndex { -1 };
    std::atomic<int> pendingIndex { -1 };
    std::atomic_flag stateFlag = ATOMIC_FLAG_INIT;

    int numTaps = 0;
    int fadeLength = 0;
    int fadePosition = 0;
    bool fading = false;
    double sampleRate = 48000.0;
};

} // namespace polarity::dsp
