#include "InversionEngine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(_M_X64) || defined(__SSE2__)
 #include <emmintrin.h>
 #define POLARITY_HAS_SSE2 1
#endif

namespace polarity::dsp
{

namespace
{
void validateKernelSize (const FirKernel& kernel, int maxTaps)
{
    const int taps = static_cast<int> (kernel.taps.size());

    if (taps < 8 || taps > maxTaps || (taps & 1) != 0 || (taps & (taps - 1)) != 0)
        throw std::invalid_argument ("kernel length must be an even power of two "
                                     "that fits the prepared maximum");
}
} // namespace

void InversionEngine::prepare (double newSampleRate, int maxTapsToUse, int numChannels)
{
    if (newSampleRate <= 0.0)
        throw std::invalid_argument ("sample rate must be positive");

    if (maxTapsToUse < 8 || (maxTapsToUse & 1) != 0
        || (maxTapsToUse & (maxTapsToUse - 1)) != 0)
        throw std::invalid_argument ("maxTaps must be an even power of two >= 8");

    sampleRate = newSampleRate;
    maxTaps = maxTapsToUse;
    fadeLength = std::max (1, static_cast<int> (std::lround (0.06 * sampleRate)));
    fadePosition = 0;
    fading = false;
    activeTaps.store (0, std::memory_order_relaxed);

    channelStates.assign (static_cast<std::size_t> (std::max (1, numChannels)),
                          ChannelState {});

    for (auto& state : channelStates)
    {
        state.history.assign (static_cast<std::size_t> (maxTaps), 0.0f);
        state.writePos = 0;
    }

    for (auto& pair : kernels)
    {
        pair.left.assign (static_cast<std::size_t> (maxTaps), 0.0f);
        pair.right.assign (static_cast<std::size_t> (maxTaps), 0.0f);
        pair.taps = 0;
    }

    currentIndex.store (0, std::memory_order_relaxed);
    previousIndex.store (-1, std::memory_order_relaxed);
    pendingIndex.store (-1, std::memory_order_relaxed);
}

void InversionEngine::reset()
{
    for (auto& state : channelStates)
    {
        std::fill (state.history.begin(), state.history.end(), 0.0f);
        state.writePos = 0;
    }
}

void InversionEngine::setKernelsImmediate (const FirKernel& left, const FirKernel& right)
{
    validateKernelSize (left, maxTaps);
    validateKernelSize (right, maxTaps);

    const int taps = static_cast<int> (left.taps.size());

    lockState();

    auto& pair = kernels[0];

    for (int i = 0; i < taps; ++i)
    {
        pair.left[static_cast<std::size_t> (i)] = static_cast<float> (
            left.taps[static_cast<std::size_t> (taps - 1 - i)]);
        pair.right[static_cast<std::size_t> (i)] = static_cast<float> (
            right.taps[static_cast<std::size_t> (taps - 1 - i)]);
    }

    pair.taps = taps;
    activeTaps.store (taps, std::memory_order_relaxed);

    currentIndex.store (0, std::memory_order_relaxed);
    previousIndex.store (-1, std::memory_order_relaxed);
    pendingIndex.store (-1, std::memory_order_relaxed);
    fading = false;
    fadePosition = 0;

    for (auto& state : channelStates)
    {
        std::fill (state.history.begin(), state.history.end(), 0.0f);
        state.writePos = 0;
    }

    unlockState();
}

void InversionEngine::setKernels (const FirKernel& left, const FirKernel& right)
{
    validateKernelSize (left, maxTaps);
    validateKernelSize (right, maxTaps);

    const int taps = static_cast<int> (left.taps.size());

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

    for (int i = 0; i < taps; ++i)
    {
        pair.left[static_cast<std::size_t> (i)] = static_cast<float> (
            left.taps[static_cast<std::size_t> (taps - 1 - i)]);
        pair.right[static_cast<std::size_t> (i)] = static_cast<float> (
            right.taps[static_cast<std::size_t> (taps - 1 - i)]);
    }

    pair.taps = taps;

    lockState();
    pendingIndex.store (slot, std::memory_order_release);
    unlockState();
}

void InversionEngine::process (float* const* channels, int numChannels, int numSamples)
{
    if (maxTaps == 0 || numSamples == 0)
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

    if (currentPair.taps == 0)
        return;

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
                        maxTaps,
                        currentKernel,
                        currentPair.taps,
                        previousKernel,
                        blockFading ? previousPair.taps : 0,
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

namespace
{
float dotProductRange (const float* samples, const float* kernel, int length) noexcept
{
    double sum = 0.0;
    int k = 0;

   #if defined(POLARITY_HAS_SSE2)
    // Accumulate in short float chunks (kept wide by SIMD) and add each chunk
    // to a double. This keeps ~-110 dB of numerical headroom without giving up
    // vectorisation.
    constexpr int chunkSize = 256;

    while (k < length)
    {
        const int chunk = std::min (chunkSize, length - k);
        __m128 acc = _mm_setzero_ps();
        int j = 0;

        for (; j + 4 <= chunk; j += 4)
            acc = _mm_add_ps (acc,
                              _mm_mul_ps (_mm_loadu_ps (samples + k + j),
                                          _mm_loadu_ps (kernel + k + j)));

        float partial[4];
        _mm_storeu_ps (partial, acc);
        sum += static_cast<double> (partial[0]) + partial[1] + partial[2] + partial[3];

        for (; j < chunk; ++j)
            sum += static_cast<double> (samples[k + j]) * kernel[k + j];

        k += chunk;
    }
   #else
    for (; k < length; ++k)
        sum += static_cast<double> (samples[k]) * kernel[k];
   #endif

    return static_cast<float> (sum);
}
} // namespace

float InversionEngine::dotProduct (const std::vector<float>& history,
                                   int writePos,
                                   int ringSize,
                                   int taps,
                                   const float* kernel) noexcept
{
    // Kernel[i] is tap[taps - 1 - i], so multiplying forwards from the oldest
    // sample used by this kernel realises the causal convolution.
    const int start = (writePos + 1 + ringSize - taps) % ringSize;
    const int firstLength = std::min (taps, ringSize - start);
    const auto* samples = history.data();

    float sum = dotProductRange (samples + start, kernel, firstLength);

    if (firstLength < taps)
        sum += dotProductRange (samples, kernel + firstLength, taps - firstLength);

    return sum;
}

void InversionEngine::processChannel (ChannelState& state,
                                     float* data,
                                     int numSamples,
                                     int ringSize,
                                     const float* currentKernel,
                                     int currentTaps,
                                     const float* previousKernel,
                                     int previousTaps,
                                     int fadeStart,
                                     bool blockFading) noexcept
{
    auto* history = state.history.data();
    int writePos = state.writePos;

    for (int i = 0; i < numSamples; ++i)
    {
        history[writePos] = data[i];

        float wet = dotProduct (state.history, writePos, ringSize,
                                currentTaps, currentKernel);

        if (blockFading && previousKernel != nullptr && previousTaps > 0)
        {
            const int position = fadeStart + i;

            if (position < fadeLength)
            {
                const float t = static_cast<float> (position)
                                / static_cast<float> (fadeLength);
                const float previousWet = dotProduct (state.history, writePos,
                                                      ringSize, previousTaps,
                                                      previousKernel);
                wet = previousWet * (1.0f - t) + wet * t;
            }
        }

        data[i] = wet;
        writePos = (writePos + 1 == ringSize) ? 0 : writePos + 1;
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
        activeTaps.store (kernels[static_cast<std::size_t> (pending)].taps,
                          std::memory_order_relaxed);
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
