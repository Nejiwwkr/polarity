#pragma once

#include "FirDesigner.h"

#include <array>
#include <atomic>
#include <complex>
#include <vector>

namespace polarity::dsp
{

/**
    Zero-additional-latency stereo FIR convolution.

    The kernel is split into a short direct head (the first hopSize taps) and
    a uniform partitioned tail.  The tail for output block m only depends on
    input block m-1 and earlier, so it can be produced at the block boundary
    and added to the per-sample head.  No input FIFO and therefore no latency
    beyond the FIR's inherent taps / 2 delay.

    Kernel replacement uses the same four-slot publish/crossfade scheme as the
    direct engine: the message thread fills a free slot and the audio thread
    crossfades to it over ~60 ms.  All buffers are allocated in prepare(); the
    audio thread only performs FFTs (preallocated scratch) and MACs.
*/
class PartitionedConvolver
{
public:
    static constexpr int hopSize = 64;
    static constexpr int fftSize = hopSize * 2;
    static constexpr int numBins = hopSize + 1;
    static constexpr int numKernelBuffers = 4;

    void prepare (double sampleRate, int maxTaps, int numChannels);
    void reset();

    /** Non-real-time: install the initial pair before audio starts. */
    void setKernelsImmediate (const FirKernel& left, const FirKernel& right);

    /** Message thread: queue a pair; the latest request wins. */
    void setKernels (const FirKernel& left, const FirKernel& right);

    /** Replaces the input history with the supplied encoded past samples
        (oldest first, ending at the last processed sample).  Audio thread;
        used when the processor switches between L/R and M/S routing. */
    void primeHistory (const float* left, const float* right, int numSamples);

    /** renderOutput == false keeps the input history warm without running the
        convolution, so bypassed output can skip the MAC/IFFT work. */
    void process (float* const* channels, int numChannels, int numSamples,
                  bool renderOutput = true);

    int getNumTaps() const noexcept
    {
        return activeTaps.load (std::memory_order_relaxed);
    }

    int getLatencySamples() const noexcept { return getNumTaps() / 2; }

private:
    struct ChannelState
    {
        std::array<double, 2 * hopSize> headHistory {};
        std::array<double, hopSize> inputAccum {};
        std::array<double, 2 * hopSize> inputFftBuffer {};
        std::vector<std::complex<double>> xRing;   // capacity * numBins
        std::array<double, hopSize> tailCurrent {};
        std::array<double, hopSize> tailPrevious {};

        int headWrite = 0;
        int blockPos = 0;
        int xWrite = 0;
        int xCount = 0;
        bool firstBlock = true;
        bool tailCurrentValid = false;
        bool tailPreviousValid = false;
    };

    struct KernelSet
    {
        std::vector<double> headLeft;   // time-reversed first hopSize taps
        std::vector<double> headRight;
        std::vector<std::complex<double>> tailLeft;   // numTail * numBins
        std::vector<std::complex<double>> tailRight;
        int taps = 0;
        int numTail = 0;
    };

    void buildKernelSet (const FirKernel& left, const FirKernel& right,
                         KernelSet& out) const;
    void consumePending();
    void lockState() noexcept;
    void unlockState() noexcept;

    void pushInputSpectrum (ChannelState&) noexcept;
    void computeTail (ChannelState&, const KernelSet&, int channel,
                      std::array<double, hopSize>& destination) noexcept;
    double headValue (const ChannelState&, int headWrite,
                      const KernelSet&, int channel) const noexcept;

    std::vector<ChannelState> channelStates;
    std::array<KernelSet, numKernelBuffers> kernels;
    std::vector<std::complex<double>> fftScratch;

    std::atomic<int> currentIndex { 0 };
    std::atomic<int> previousIndex { -1 };
    std::atomic<int> pendingIndex { -1 };
    std::atomic<int> activeTaps { 0 };
    std::atomic_flag stateFlag = ATOMIC_FLAG_INIT;

    int maxTaps = 0;
    int capacity = 0;
    int fadeLength = 0;
    int fadePosition = 0;
    bool fading = false;
    double sampleRate = 48000.0;
};

} // namespace polarity::dsp
