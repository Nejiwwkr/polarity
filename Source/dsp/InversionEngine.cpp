#include "InversionEngine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace polarity::dsp
{

namespace
{
void validateKernelSize (const FirKernel& kernel, int numTaps)
{
    if (static_cast<int> (kernel.taps.size()) != numTaps)
        throw std::invalid_argument ("kernel length does not match prepared engine");
}
} // namespace

void InversionEngine::prepare (double newSampleRate, int taps, int numChannels)
{
    if (newSampleRate <= 0.0)
        throw std::invalid_argument ("sample rate must be positive");

    if (taps < 8 || (taps & 1) != 0 || (taps & (taps - 1)) != 0)
        throw std::invalid_argument ("numTaps must be an even power of two >= 8");

    sampleRate = newSampleRate;
    numTaps = taps;
    fadeLength = std::max (1, static_cast<int> (std::lround (0.06 * sampleRate)));
    fadePosition = 0;
    fading = false;

    channelStates.assign (static_cast<std::size_t> (std::max (1, numChannels)),
                          ChannelState {});

    for (auto& state : channelStates)
    {
        state.history.assign (static_cast<std::size_t> (numTaps), 0.0);
        state.writePos = 0;
    }

    for (auto& pair : kernels)
    {
        pair.left.assign (static_cast<std::size_t> (numTaps), 0.0);
        pair.right.assign (static_cast<std::size_t> (numTaps), 0.0);
    }

    currentIndex.store (0, std::memory_order_relaxed);
    previousIndex.store (-1, std::memory_order_relaxed);
    pendingIndex.store (-1, std::memory_order_relaxed);
}

void InversionEngine::reset()
{
    for (auto& state : channelStates)
    {
        std::fill (state.history.begin(), state.history.end(), 0.0);
        state.writePos = 0;
    }
}

void InversionEngine::setKernelsImmediate (const FirKernel& left, const FirKernel& right)
{
    validateKernelSize (left, numTaps);
    validateKernelSize (right, numTaps);

    lockState();

    auto& pair = kernels[0];

    for (int i = 0; i < numTaps; ++i)
    {
        pair.left[static_cast<std::size_t> (i)]
            = left.taps[static_cast<std::size_t> (numTaps - 1 - i)];
        pair.right[static_cast<std::size_t> (i)]
            = right.taps[static_cast<std::size_t> (numTaps - 1 - i)];
    }

    currentIndex.store (0, std::memory_order_relaxed);
    previousIndex.store (-1, std::memory_order_relaxed);
    pendingIndex.store (-1, std::memory_order_relaxed);
    fading = false;
    fadePosition = 0;

    for (auto& state : channelStates)
    {
        std::fill (state.history.begin(), state.history.end(), 0.0);
        state.writePos = 0;
    }

    unlockState();
}

void InversionEngine::setKernels (const FirKernel& left, const FirKernel& right)
{
    validateKernelSize (left, numTaps);
    validateKernelSize (right, numTaps);

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

    auto& pair = kernels[static_cast<std::size_t> (slot)];

    for (int i = 0; i < numTaps; ++i)
    {
        pair.left[static_cast<std::size_t> (i)]
            = left.taps[static_cast<std::size_t> (numTaps - 1 - i)];
        pair.right[static_cast<std::size_t> (i)]
            = right.taps[static_cast<std::size_t> (numTaps - 1 - i)];
    }

    lockState();
    pendingIndex.store (slot, std::memory_order_release);
    unlockState();
}

void InversionEngine::process (float* const* channels, int numChannels, int numSamples)
{
    if (numTaps == 0 || numSamples == 0)
        return;

    consumePending();

    const int count = std::min (numChannels,
                                static_cast<int> (channelStates.size()));
    if (count == 0)
        return;

    const int current = currentIndex.load (std::memory_order_acquire);
    const int previous = previousIndex.load (std::memory_order_acquire);
    const bool blockFading = fading && previous >= 0;

    const auto& currentPair = kernels[static_cast<std::size_t> (current)];
    const auto& previousPair = kernels[static_cast<std::size_t> (std::max (previous, 0))];

    const int fadeStart = fadePosition;

    for (int channel = 0; channel < count; ++channel)
    {
        if (channels[channel] == nullptr)
            continue;

        const auto* currentKernel = channel == 1 ? currentPair.right.data()
                                                 : currentPair.left.data();
        const auto* previousKernel = blockFading
                                         ? (channel == 1 ? previousPair.right.data()
                                                         : previousPair.left.data())
                                         : nullptr;

        processChannel (channelStates[static_cast<std::size_t> (channel)],
                        channels[channel],
                        numSamples,
                        currentKernel,
                        previousKernel,
                        fadeStart,
                        blockFading);
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

double InversionEngine::dotProduct (const std::vector<double>& history,
                                   int writePos,
                                   const double* kernel) noexcept
{
    const int size = static_cast<int> (history.size());
    const int firstStart = writePos + 1;
    const int firstLength = size - firstStart;
    const int secondLength = writePos + 1;

    double sum = 0.0;

    for (int k = 0; k < firstLength; ++k)
        sum += kernel[k] * history[static_cast<std::size_t> (firstStart + k)];

    for (int k = 0; k < secondLength; ++k)
        sum += kernel[firstLength + k] * history[static_cast<std::size_t> (k)];

    return sum;
}

void InversionEngine::processChannel (ChannelState& state,
                                     float* data,
                                     int numSamples,
                                     const double* currentKernel,
                                     const double* previousKernel,
                                     int fadeStart,
                                     bool blockFading) noexcept
{
    const int N = numTaps;
    auto* history = state.history.data();
    int writePos = state.writePos;

    for (int i = 0; i < numSamples; ++i)
    {
        history[writePos] = static_cast<double> (data[i]);

        double wet = dotProduct (state.history, writePos, currentKernel);

        if (blockFading)
        {
            const int position = fadeStart + i;

            if (position < fadeLength)
            {
                const double t = static_cast<double> (position)
                                 / static_cast<double> (fadeLength);
                const double previousWet = dotProduct (state.history, writePos,
                                                       previousKernel);
                wet = previousWet * (1.0 - t) + wet * t;
            }
        }

        data[i] = static_cast<float> (wet);
        writePos = (writePos + 1 == N) ? 0 : writePos + 1;
    }

    state.writePos = writePos;
}

void InversionEngine::consumePending()
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
        fading = true;
        fadePosition = 0;
    }

    unlockState();
}

void InversionEngine::lockState() noexcept
{
    while (stateFlag.test_and_set (std::memory_order_acquire))
    {
    }
}

void InversionEngine::unlockState() noexcept
{
    stateFlag.clear (std::memory_order_release);
}

} // namespace polarity::dsp
