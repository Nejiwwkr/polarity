#include "PartitionedConvolver.h"

#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace polarity::dsp
{

namespace
{
void validateKernel (const FirKernel& kernel, int maxTaps)
{
    const int taps = static_cast<int> (kernel.taps.size());

    if (taps < 8 || taps > maxTaps || (taps & (taps - 1)) != 0
        || taps % PartitionedConvolver::hopSize != 0)
    {
        throw std::invalid_argument ("kernel length must be a power of two, "
                                     "a multiple of the hop size, and fit the "
                                     "prepared maximum");
    }
}
} // namespace

void PartitionedConvolver::prepare (double newSampleRate, int maxTapsToUse,
                                    int numChannels)
{
    if (newSampleRate <= 0.0)
        throw std::invalid_argument ("sample rate must be positive");

    if (maxTapsToUse < hopSize || (maxTapsToUse & (maxTapsToUse - 1)) != 0
        || maxTapsToUse % hopSize != 0)
    {
        throw std::invalid_argument ("maxTaps must be a power of two multiple "
                                     "of the hop size");
    }

    sampleRate = newSampleRate;
    maxTaps = maxTapsToUse;
    capacity = std::max (1, maxTaps / hopSize);
    fadeLength = std::max (1, static_cast<int> (std::lround (0.06 * sampleRate)));
    fadePosition = 0;
    fading = false;
    activeTaps.store (0, std::memory_order_relaxed);

    fftScratch.assign (static_cast<std::size_t> (fftSize), {});

    channelStates.assign (static_cast<std::size_t> (std::max (1, numChannels)),
                          ChannelState {});

    for (auto& state : channelStates)
    {
        state.headHistory.fill (0.0);
        state.inputAccum.fill (0.0);
        state.inputFftBuffer.fill (0.0);
        state.xRing.assign (static_cast<std::size_t> (capacity) * numBins, {});
        state.tailCurrent.fill (0.0);
        state.tailPrevious.fill (0.0);
        state.headWrite = 0;
        state.blockPos = 0;
        state.xWrite = 0;
        state.xCount = 0;
        state.firstBlock = true;
        state.tailCurrentValid = false;
        state.tailPreviousValid = false;
    }

    for (auto& kernel : kernels)
    {
        kernel.headLeft.assign (hopSize, 0.0);
        kernel.headRight.assign (hopSize, 0.0);
        kernel.tailLeft.clear();
        kernel.tailRight.clear();
        kernel.taps = 0;
        kernel.numTail = 0;
    }

    currentIndex.store (0, std::memory_order_relaxed);
    previousIndex.store (-1, std::memory_order_relaxed);
    pendingIndex.store (-1, std::memory_order_relaxed);
}

void PartitionedConvolver::reset()
{
    for (auto& state : channelStates)
    {
        state.headHistory.fill (0.0);
        state.inputAccum.fill (0.0);
        state.inputFftBuffer.fill (0.0);
        std::fill (state.xRing.begin(), state.xRing.end(),
                   std::complex<double> {});
        state.tailCurrent.fill (0.0);
        state.tailPrevious.fill (0.0);
        state.headWrite = 0;
        state.blockPos = 0;
        state.xWrite = 0;
        state.xCount = 0;
        state.firstBlock = true;
        state.tailCurrentValid = false;
        state.tailPreviousValid = false;
    }
}

void PartitionedConvolver::buildKernelSet (const FirKernel& left,
                                           const FirKernel& right,
                                           KernelSet& out) const
{
    const int taps = static_cast<int> (left.taps.size());

    out.taps = taps;
    out.numTail = std::max (0, taps / hopSize - 1);

    out.headLeft.assign (hopSize, 0.0);
    out.headRight.assign (hopSize, 0.0);

    // Stored time-reversed so the forward dot product over the oldest-to-newest
    // input window realises the causal head convolution.
    for (int i = 0; i < hopSize; ++i)
    {
        out.headLeft[static_cast<std::size_t> (i)]
            = left.taps[static_cast<std::size_t> (hopSize - 1 - i)];
        out.headRight[static_cast<std::size_t> (i)]
            = right.taps[static_cast<std::size_t> (hopSize - 1 - i)];
    }

    out.tailLeft.assign (static_cast<std::size_t> (out.numTail) * numBins, {});
    out.tailRight.assign (static_cast<std::size_t> (out.numTail) * numBins, {});

    std::vector<std::complex<double>> work (static_cast<std::size_t> (fftSize));

    for (int r = 0; r < out.numTail; ++r)
    {
        std::fill (work.begin(), work.end(), std::complex<double> {});

        const int firstTap = hopSize + r * hopSize;

        for (int j = 0; j < hopSize; ++j)
        {
            const int tap = firstTap + j;

            if (tap >= taps)
                break;

            work[static_cast<std::size_t> (j)] = std::complex<double> (
                left.taps[static_cast<std::size_t> (tap)], 0.0);
        }

        fftInPlace (work, false);

        auto* destination = out.tailLeft.data()
                            + static_cast<std::size_t> (r) * numBins;
        std::copy (work.begin(), work.begin() + numBins, destination);

        std::fill (work.begin(), work.end(), std::complex<double> {});
        const int firstRight = hopSize + r * hopSize;

        for (int j = 0; j < hopSize; ++j)
        {
            const int tap = firstRight + j;

            if (tap >= taps)
                break;

            work[static_cast<std::size_t> (j)] = std::complex<double> (
                right.taps[static_cast<std::size_t> (tap)], 0.0);
        }

        fftInPlace (work, false);

        auto* destinationRight = out.tailRight.data()
                                 + static_cast<std::size_t> (r) * numBins;
        std::copy (work.begin(), work.begin() + numBins, destinationRight);
    }
}

void PartitionedConvolver::setKernelsImmediate (const FirKernel& left,
                                                const FirKernel& right)
{
    validateKernel (left, maxTaps);
    validateKernel (right, maxTaps);

    if (left.taps.size() != right.taps.size())
        throw std::invalid_argument ("left and right kernels must match");

    buildKernelSet (left, right, kernels[0]);

    lockState();
    activeTaps.store (kernels[0].taps, std::memory_order_relaxed);
    currentIndex.store (0, std::memory_order_relaxed);
    previousIndex.store (-1, std::memory_order_relaxed);
    pendingIndex.store (-1, std::memory_order_relaxed);
    fading = false;
    fadePosition = 0;
    unlockState();

    reset();
}

void PartitionedConvolver::setKernels (const FirKernel& left,
                                       const FirKernel& right)
{
    validateKernel (left, maxTaps);
    validateKernel (right, maxTaps);

    if (left.taps.size() != right.taps.size())
        throw std::invalid_argument ("left and right kernels must match");

    int slot = -1;

    lockState();

    {
        const int current = currentIndex.load (std::memory_order_relaxed);
        const int previous = previousIndex.load (std::memory_order_relaxed);
        const int pending = pendingIndex.load (std::memory_order_relaxed);

        for (int i = 0; i < numKernelBuffers; ++i)
        {
            if (i != current && i != previous && i != pending)
            {
                slot = i;
                break;
            }
        }
    }

    unlockState();

    if (slot < 0)
        return;

    buildKernelSet (left, right, kernels[static_cast<std::size_t> (slot)]);

    lockState();
    pendingIndex.store (slot, std::memory_order_release);
    unlockState();
}

void PartitionedConvolver::primeHistory (const float* left, const float* right,
                                         int numSamples)
{
    if (maxTaps == 0 || numSamples <= 0)
        return;

    for (int channel = 0; channel < static_cast<int> (channelStates.size()); ++channel)
    {
        auto& state = channelStates[static_cast<std::size_t> (channel)];
        const float* past = channel == 1 ? right : left;

        if (past == nullptr)
            past = left;

        if (past == nullptr)
            continue;

        const int blockPos = std::clamp (state.blockPos, 0, hopSize - 1);

        state.headHistory.fill (0.0);

        for (int j = 0; j < hopSize; ++j)
        {
            const int p = numSamples - hopSize + j;
            const double value = p >= 0 ? static_cast<double> (past[p]) : 0.0;
            const int position = (blockPos + j) % hopSize;
            state.headHistory[static_cast<std::size_t> (position)] = value;
            state.headHistory[static_cast<std::size_t> (position + hopSize)] = value;
        }

        // The engine state differs depending on where we are inside a hop:
        //  - blockPos == 0: the next sample will push X for the just-completed
        //    block, so the buffers must hold that block and the X ring must end
        //    at the block before it;
        //  - blockPos > 0: the current block's push has already happened, so
        //    the buffers hold [block m-2, block m-1] and the ring includes X_{m-1}.
        const int anchor = numSamples - blockPos - 1;

        state.inputAccum.fill (0.0);

        if (blockPos == 0)
        {
            for (int j = 0; j < hopSize; ++j)
            {
                const int p = anchor - hopSize + 1 + j;

                if (p >= 0)
                    state.inputAccum[static_cast<std::size_t> (j)] = past[p];
            }
        }
        else
        {
            for (int j = 0; j < blockPos; ++j)
            {
                const int p = anchor + 1 + j;

                if (p >= 0)
                    state.inputAccum[static_cast<std::size_t> (j)] = past[p];
            }
        }

        state.inputFftBuffer.fill (0.0);

        const auto fillBlock = [&] (int destinationOffset, int blockEnd)
        {
            for (int j = 0; j < hopSize; ++j)
            {
                const int p = blockEnd - hopSize + 1 + j;

                if (p >= 0)
                    state.inputFftBuffer[static_cast<std::size_t> (
                        destinationOffset + j)] = past[p];
            }
        };

        if (blockPos == 0)
        {
            // [block m-3, block m-2]
            fillBlock (hopSize, anchor - hopSize);
            fillBlock (0, anchor - 2 * hopSize);
        }
        else
        {
            // [block m-2, block m-1]
            fillBlock (hopSize, anchor);
            fillBlock (0, anchor - hopSize);
        }

        std::fill (state.xRing.begin(), state.xRing.end(),
                   std::complex<double> {});

        const int newestEnd = blockPos == 0 ? anchor - hopSize : anchor;
        int available = 0;

        for (int end = newestEnd; end >= 0 && available < capacity;
             end -= hopSize)
        {
            ++available;
        }

        state.xWrite = 0;
        state.xCount = 0;

        if (available > 0)
        {
            const int oldestEnd = newestEnd - (available - 1) * hopSize;

            for (int block = 0; block < available; ++block)
            {
                const int end = oldestEnd + block * hopSize;

                for (int j = 0; j < fftSize; ++j)
                {
                    const int p = end - fftSize + 1 + j;
                    fftScratch[static_cast<std::size_t> (j)]
                        = p >= 0 ? std::complex<double> (
                                       static_cast<double> (past[p]), 0.0)
                                 : std::complex<double> {};
                }

                fftInPlace (fftScratch, false);

                auto* destination = state.xRing.data()
                                    + static_cast<std::size_t> (state.xWrite)
                                          * numBins;
                std::copy (fftScratch.begin(),
                           fftScratch.begin() + numBins,
                           destination);

                state.xWrite = (state.xWrite + 1) % capacity;
                state.xCount = std::min (state.xCount + 1, capacity);
            }
        }

        state.firstBlock = false;
        state.tailCurrentValid = false;
        state.tailPreviousValid = false;
    }
}

void PartitionedConvolver::pushInputSpectrum (ChannelState& state) noexcept
{
    std::copy (state.inputFftBuffer.begin() + hopSize,
               state.inputFftBuffer.end(),
               state.inputFftBuffer.begin());
    std::copy (state.inputAccum.begin(), state.inputAccum.end(),
               state.inputFftBuffer.begin() + hopSize);

    for (int i = 0; i < fftSize; ++i)
        fftScratch[static_cast<std::size_t> (i)]
            = std::complex<double> (state.inputFftBuffer[static_cast<std::size_t> (i)],
                                    0.0);

    fftInPlace (fftScratch, false);

    auto* destination = state.xRing.data()
                        + static_cast<std::size_t> (state.xWrite) * numBins;
    std::copy (fftScratch.begin(), fftScratch.begin() + numBins, destination);

    state.xWrite = (state.xWrite + 1) % capacity;
    state.xCount = std::min (state.xCount + 1, capacity);
}

void PartitionedConvolver::computeTail (ChannelState& state,
                                         const KernelSet& kernel,
                                         int channel,
                                         std::array<double, hopSize>& destination) noexcept
{
    std::fill (fftScratch.begin(), fftScratch.end(), std::complex<double> {});

    const auto* tail = channel == 1 ? kernel.tailRight.data()
                                    : kernel.tailLeft.data();
    const int available = std::min (kernel.numTail, state.xCount);

    for (int r = 0; r < available; ++r)
    {
        const int index = (state.xWrite - 1 - r + capacity) % capacity;
        const auto* x = state.xRing.data()
                        + static_cast<std::size_t> (index) * numBins;
        const auto* h = tail + static_cast<std::size_t> (r) * numBins;

        for (int k = 0; k < numBins; ++k)
            fftScratch[static_cast<std::size_t> (k)]
                += h[static_cast<std::size_t> (k)]
                   * x[static_cast<std::size_t> (k)];
    }

    // Rebuild the conjugate-symmetric upper half before the inverse transform.
    for (int k = 1; k < numBins - 1; ++k)
        fftScratch[static_cast<std::size_t> (fftSize - k)]
            = std::conj (fftScratch[static_cast<std::size_t> (k)]);

    fftInPlace (fftScratch, true);

    for (int t = 0; t < hopSize; ++t)
        destination[static_cast<std::size_t> (t)]
            = fftScratch[static_cast<std::size_t> (hopSize + t)].real();
}

double PartitionedConvolver::headValue (const ChannelState& state,
                                        int headWrite,
                                        const KernelSet& kernel,
                                        int channel) const noexcept
{
    const double* window = state.headHistory.data() + headWrite + 1;
    const auto& head = channel == 1 ? kernel.headRight : kernel.headLeft;

    double sum = 0.0;

    for (int i = 0; i < hopSize; ++i)
        sum += window[i] * head[static_cast<std::size_t> (i)];

    return sum;
}

void PartitionedConvolver::process (float* const* channels, int numChannels,
                                    int numSamples, bool renderOutput)
{
    if (maxTaps == 0 || numSamples == 0)
        return;

    consumePending();

    const int count = std::min (numChannels,
                                static_cast<int> (channelStates.size()));

    if (count == 0)
        return;

    if (! renderOutput)
    {
        // Keep history current but do not start a fade that cannot be heard.
        lockState();
        previousIndex.store (-1, std::memory_order_relaxed);
        fading = false;
        fadePosition = 0;
        unlockState();
    }

    const int current = currentIndex.load (std::memory_order_acquire);
    const int previous = previousIndex.load (std::memory_order_acquire);
    const bool blockFading = renderOutput && fading && previous >= 0;

    const auto& currentKernel = kernels[static_cast<std::size_t> (current)];
    const auto& previousKernel
        = kernels[static_cast<std::size_t> (std::max (previous, 0))];

    const bool canRender = renderOutput && currentKernel.taps > 0;
    const int fadeStart = fadePosition;

    for (int channel = 0; channel < count; ++channel)
    {
        auto* data = channels[channel];

        if (data == nullptr)
            continue;

        auto& state = channelStates[static_cast<std::size_t> (channel)];

        int headWrite = state.headWrite;
        int blockPos = state.blockPos;

        for (int i = 0; i < numSamples; ++i)
        {
            if (blockPos == 0)
            {
                if (! state.firstBlock)
                    pushInputSpectrum (state);
                else
                    state.firstBlock = false;

                state.tailCurrentValid = false;
                state.tailPreviousValid = false;
            }

            const double x = static_cast<double> (data[i]);

            state.headHistory[static_cast<std::size_t> (headWrite)] = x;
            state.headHistory[static_cast<std::size_t> (headWrite + hopSize)] = x;
            state.inputAccum[static_cast<std::size_t> (blockPos)] = x;

            if (canRender)
            {
                if (! state.tailCurrentValid)
                {
                    computeTail (state, currentKernel, channel, state.tailCurrent);
                    state.tailCurrentValid = true;
                }

                double wet = headValue (state, headWrite, currentKernel, channel)
                             + state.tailCurrent[static_cast<std::size_t> (blockPos)];

                if (blockFading && previousKernel.taps > 0)
                {
                    if (! state.tailPreviousValid)
                    {
                        computeTail (state, previousKernel, channel,
                                     state.tailPrevious);
                        state.tailPreviousValid = true;
                    }

                    const double previousWet
                        = headValue (state, headWrite, previousKernel, channel)
                          + state.tailPrevious[static_cast<std::size_t> (blockPos)];

                    const int position = fadeStart + i;
                    const double t = position >= fadeLength
                                         ? 1.0
                                         : static_cast<double> (position)
                                               / static_cast<double> (fadeLength);

                    wet = previousWet * (1.0 - t) + wet * t;
                }

                data[i] = static_cast<float> (wet);
            }
            else
            {
                state.tailCurrentValid = false;
                state.tailPreviousValid = false;

                if (! renderOutput)
                {
                    // The caller keeps the dry signal; nothing to write here.
                }
            }

            headWrite = (headWrite + 1) % hopSize;
            blockPos = (blockPos + 1) % hopSize;
        }

        state.headWrite = headWrite;
        state.blockPos = blockPos;
    }

    if (blockFading)
    {
        fadePosition += numSamples;

        if (fadePosition >= fadeLength)
        {
            fadePosition = 0;
            fading = false;

            lockState();
            previousIndex.store (-1, std::memory_order_release);
            unlockState();
        }
    }
}

void PartitionedConvolver::consumePending()
{
    if (fading)
        return;

    lockState();

    const int pending = pendingIndex.load (std::memory_order_acquire);

    if (pending >= 0)
    {
        previousIndex.store (currentIndex.load (std::memory_order_relaxed),
                             std::memory_order_relaxed);
        currentIndex.store (pending, std::memory_order_relaxed);
        pendingIndex.store (-1, std::memory_order_relaxed);
        activeTaps.store (kernels[static_cast<std::size_t> (pending)].taps,
                          std::memory_order_relaxed);
        fading = true;
        fadePosition = 0;

        // A mid-block swap invalidates tails that were computed for the old
        // current/previous kernels.
        for (auto& state : channelStates)
        {
            state.tailCurrentValid = false;
            state.tailPreviousValid = false;
        }
    }

    unlockState();
}

void PartitionedConvolver::lockState() noexcept
{
    while (stateFlag.test_and_set (std::memory_order_acquire))
    {
    }
}

void PartitionedConvolver::unlockState() noexcept
{
    stateFlag.clear (std::memory_order_release);
}

} // namespace polarity::dsp
