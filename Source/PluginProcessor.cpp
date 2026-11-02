#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "dsp/FirDesigner.h"

#include <algorithm>
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout
PolarityAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 },
        "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        1.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 },
        "Bypass",
        false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "deltaOut", 1 },
        "Delta Out",
        false));

    return layout;
}

std::vector<polarity::dsp::CurvePoint> PolarityAudioProcessor::defaultCurvePoints()
{
    return { { 20.0, 0.0 }, { 20000.0, 0.0 } };
}

juce::ValueTree PolarityAudioProcessor::curveToTree (
    const juce::String& channel,
    const polarity::dsp::CurveModel& curve)
{
    juce::ValueTree tree ("CURVE");
    tree.setProperty ("channel", channel, nullptr);

    for (const auto& point : curve.getPoints())
    {
        juce::ValueTree pointTree ("POINT");
        pointTree.setProperty ("f", point.freqHz, nullptr);
        pointTree.setProperty ("a", point.amount, nullptr);
        tree.appendChild (pointTree, nullptr);
    }

    return tree;
}

void PolarityAudioProcessor::readCurve (
    const juce::ValueTree& parent,
    const juce::String& channel,
    polarity::dsp::CurveModel& model)
{
    std::vector<polarity::dsp::CurvePoint> points;
    const auto tree = parent.getChildWithProperty ("channel", channel);

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto point = tree.getChild (i);

        if (point.hasType ("POINT"))
            points.push_back ({
                static_cast<double> (point.getProperty ("f")),
                static_cast<double> (point.getProperty ("a"))
            });
    }

    if (points.size() >= 2)
        model.setPoints (std::move (points));
    else
        model.setPoints (defaultCurvePoints());
}

PolarityAudioProcessor::PolarityAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    mixParam = apvts.getRawParameterValue ("mix");
    bypassParam = apvts.getRawParameterValue ("bypass");
    deltaOutParam = apvts.getRawParameterValue ("deltaOut");

    for (auto& slot : slots)
    {
        slot.left.setPoints (defaultCurvePoints());
        slot.right.setPoints (defaultCurvePoints());
    }

    writeCurvesToState();
    resetAnalysis();
}

void PolarityAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);

    qualityChoice.store (qualityFromState());
    const int taps = getQualityTaps (qualityChoice.load());

    engine.prepare (sampleRate, maxTaps, 2);

    polarity::dsp::CurveModel left;
    polarity::dsp::CurveModel right;

    {
        const juce::ScopedLock lock (curveLock);
        const auto& slot = slots[static_cast<std::size_t> (activeSlot.load())];
        left = slot.left;
        right = slot.right;
    }

    const int edgeSmoothing = curveSmoothingBins.load (std::memory_order_relaxed);

    engine.setKernelsImmediate (
        polarity::dsp::designKernel (left, sampleRate, taps, edgeSmoothing),
        polarity::dsp::designKernel (right, sampleRate, taps, edgeSmoothing));

    activeLatency.store (taps / 2, std::memory_order_relaxed);
    setLatencySamples (taps / 2);

    analysisWork.assign (static_cast<std::size_t> (fftSize * 2), 0.0f);
    hannWindow.resize (static_cast<std::size_t> (fftSize));

    constexpr float twoPi = 6.283185307179586f;

    for (int i = 0; i < fftSize; ++i)
        hannWindow[static_cast<std::size_t> (i)]
            = 0.5f - 0.5f * std::cos (twoPi * static_cast<float> (i)
                                     / static_cast<float> (fftSize - 1));

    maxWaveformSamples = std::max (1024, static_cast<int> (
                                          std::lround (sampleRate * 5.0)));
    waveformUpdateCounter = 0;

    inputAnalysisRing.assign (
        static_cast<std::size_t> (maxWaveformSamples + maxTaps / 2), 0.0f);
    outputAnalysisRing.assign (
        static_cast<std::size_t> (maxWaveformSamples), 0.0f);
    inputAnalysisWrite = 0;
    outputAnalysisWrite = 0;
    resetAnalysis();

    dryCopy.setSize (2, juce::jmax (1, samplesPerBlock), false, true, false);
    dryCopy.clear();

    dryDelay.assign (2, std::vector<float> (
                            static_cast<std::size_t> (maxTaps / 2), 0.0f));
    dryDelayIndex.assign (2, 0);

    deltaSmoothingCoeff = static_cast<float> (
        1.0 - std::exp (-1.0 / (0.03 * sampleRate)));
    const float deltaInitial = deltaOutParam != nullptr ? deltaOutParam->load() : 0.0f;
    deltaAmounts = { deltaInitial, deltaInitial };
}

void PolarityAudioProcessor::releaseResources()
{
    engine.reset();
}

bool PolarityAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input  = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    return input == output
        && (input == juce::AudioChannelSet::mono()
            || input == juce::AudioChannelSet::stereo());
}

void PolarityAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int processedChannels = juce::jmin (numChannels, 2);

    for (int channel = processedChannels; channel < numChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples == 0 || processedChannels == 0)
        return;

    jassert (numSamples <= dryCopy.getNumSamples());

    for (int channel = 0; channel < processedChannels; ++channel)
        dryCopy.copyFrom (channel, 0, buffer, channel, 0, numSamples);

    float* pointers[2] = {
        buffer.getWritePointer (0),
        processedChannels > 1 ? buffer.getWritePointer (1) : nullptr
    };

    engine.process (pointers, processedChannels, numSamples);

    const float mix = mixParam != nullptr ? mixParam->load() : 1.0f;
    const bool bypassed = bypassParam != nullptr && bypassParam->load() >= 0.5f;
    const float deltaTarget = ! bypassed && deltaOutParam != nullptr
                                  && deltaOutParam->load() >= 0.5f
                              ? 1.0f : 0.0f;

    const int latency = juce::jlimit (
        0, static_cast<int> (dryDelay.empty() ? 0 : dryDelay[0].size()),
        activeLatency.load (std::memory_order_relaxed));

    for (int channel = 0; channel < processedChannels; ++channel)
    {
        auto* output = buffer.getWritePointer (channel);
        auto& delay = dryDelay[static_cast<std::size_t> (channel)];
        int& storedIndex = dryDelayIndex[static_cast<std::size_t> (channel)];
        const int delaySize = static_cast<int> (delay.size());
        float& deltaAmount = deltaAmounts[static_cast<std::size_t> (channel)];

        if (delaySize <= 0)
            continue;

        int writeIndex = storedIndex;
        int readIndex = (storedIndex + delaySize - latency) % delaySize;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float dry = delay[static_cast<std::size_t> (readIndex)];
            delay[static_cast<std::size_t> (writeIndex)]
                = dryCopy.getSample (channel, sample);

            writeIndex = (writeIndex + 1 == delaySize) ? 0 : writeIndex + 1;
            readIndex = (readIndex + 1 == delaySize) ? 0 : readIndex + 1;

            const float wet = output[sample];
            const float normal = wet * mix + dry * (1.0f - mix);
            const float delta = dry - normal;

            deltaAmount += (deltaTarget - deltaAmount) * deltaSmoothingCoeff;

            output[sample] = bypassed ? dry
                                      : normal + deltaAmount * (delta - normal);
        }

        storedIndex = writeIndex;
    }

    // --- post-output M/S metering ---------------------------------------
    {
        const auto* left = buffer.getReadPointer (0);
        const auto* right = processedChannels > 1 ? buffer.getReadPointer (1) : left;

        double sumL = 0.0, sumR = 0.0, sumLR = 0.0, sumMid = 0.0, sumSide = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const double l = left[i];
            const double r = right[i];
            const double mid = 0.5 * (l + r);
            const double side = 0.5 * (l - r);

            sumL += l * l;
            sumR += r * r;
            sumLR += l * r;
            sumMid += mid * mid;
            sumSide += side * side;
        }

        const double n = static_cast<double> (numSamples);
        const double midRms = std::sqrt (sumMid / n);
        const double sideRms = std::sqrt (sumSide / n);
        const double correlation = (sumL > 1.0e-12 && sumR > 1.0e-12)
                                       ? sumLR / std::sqrt (sumL * sumR)
                                       : 0.0;

        const auto toDb = [] (double value)
        {
            return static_cast<float> (20.0 * std::log10 (std::max (value, 1.0e-7)));
        };

        const float midDb = toDb (midRms);
        const float sideDb = toDb (sideRms);

        meterMidDb.store (std::clamp (meterMidDb.load (std::memory_order_relaxed) * 0.75f
                                          + midDb * 0.25f,
                                      -72.0f, 6.0f),
                          std::memory_order_relaxed);
        meterSideDb.store (std::clamp (meterSideDb.load (std::memory_order_relaxed) * 0.75f
                                           + sideDb * 0.25f,
                                       -72.0f, 6.0f),
                           std::memory_order_relaxed);
        meterCorrelation.store (std::clamp (
                                    meterCorrelation.load (std::memory_order_relaxed) * 0.8f
                                        + static_cast<float> (correlation) * 0.2f,
                                    -1.0f, 1.0f),
                                std::memory_order_relaxed);
    }

    // --- analysis feeds (input aligned to the wet path's latency) --------
    {
        const int ringSize = static_cast<int> (inputAnalysisRing.size());
        const int outputSize = static_cast<int> (outputAnalysisRing.size());

        if (ringSize > 0 && outputSize > 0)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float inL = dryCopy.getSample (0, i);
                const float inR = processedChannels > 1 ? dryCopy.getSample (1, i) : inL;
                inputAnalysisRing[static_cast<std::size_t> (inputAnalysisWrite)]
                    = 0.5f * (inL + inR);
                inputAnalysisWrite = (inputAnalysisWrite + 1) % ringSize;

                const float outL = buffer.getSample (0, i);
                const float outR = processedChannels > 1 ? buffer.getSample (1, i) : outL;
                outputAnalysisRing[static_cast<std::size_t> (outputAnalysisWrite)]
                    = 0.5f * (outL + outR);
                outputAnalysisWrite = (outputAnalysisWrite + 1) % outputSize;
            }

            updateAnalysis (engine.getLatencySamples(), numSamples);
        }
    }
}

juce::AudioProcessorEditor* PolarityAudioProcessor::createEditor()
{
    return new PolarityAudioProcessorEditor (*this);
}

juce::AudioProcessorParameter* PolarityAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter ("bypass");
}

std::vector<polarity::dsp::CurvePoint> PolarityAudioProcessor::getCurvePoints (int channel) const
{
    const juce::ScopedLock lock (curveLock);
    const auto& slot = slots[static_cast<std::size_t> (activeSlot.load())];
    return (channel == 1 ? slot.right : slot.left).getPoints();
}

void PolarityAudioProcessor::setCurvePoints (
    int channel,
    std::vector<polarity::dsp::CurvePoint> points)
{
    if (channel != 0 && channel != 1)
        return;

    auto left = getCurvePoints (0);
    auto right = getCurvePoints (1);

    if (channel == 1)
        right = std::move (points);
    else
        left = std::move (points);

    setCurves (std::move (left), std::move (right));
}

void PolarityAudioProcessor::setCurves (
    std::vector<polarity::dsp::CurvePoint> leftPoints,
    std::vector<polarity::dsp::CurvePoint> rightPoints)
{
    {
        const juce::ScopedLock lock (curveLock);
        auto& slot = slots[static_cast<std::size_t> (activeSlot.load())];
        slot.left.setPoints (std::move (leftPoints));
        slot.right.setPoints (std::move (rightPoints));
        writeCurvesToState();
    }

    if (currentSampleRate.load() > 0.0)
        rebuildKernels();
}

void PolarityAudioProcessor::setActiveSlot (int slot)
{
    if (slot != 0 && slot != 1)
        return;

    {
        const juce::ScopedLock lock (curveLock);

        if (activeSlot.load() == slot)
            return;

        activeSlot.store (slot);
        writeCurvesToState();
    }

    if (currentSampleRate.load() > 0.0)
        rebuildKernels();

    curveBroadcaster.sendChangeMessage();
}

void PolarityAudioProcessor::writeCurvesToState()
{
    auto curves = apvts.state.getChildWithName ("CURVES");

    if (! curves.isValid())
    {
        curves = juce::ValueTree ("CURVES");
        apvts.state.appendChild (curves, nullptr);
    }

    curves.setProperty ("active", activeSlot.load(), nullptr);
    curves.removeAllChildren (nullptr);

    for (int i = 0; i < 2; ++i)
    {
        juce::ValueTree slot ("SLOT");
        slot.setProperty ("index", i, nullptr);
        slot.appendChild (curveToTree ("L", slots[static_cast<std::size_t> (i)].left), nullptr);
        slot.appendChild (curveToTree ("R", slots[static_cast<std::size_t> (i)].right), nullptr);
        curves.appendChild (slot, nullptr);
    }
}

void PolarityAudioProcessor::loadCurvesFromState()
{
    std::array<CurveSlot, 2> loaded;

    for (auto& slot : loaded)
    {
        slot.left.setPoints (defaultCurvePoints());
        slot.right.setPoints (defaultCurvePoints());
    }

    int active = 0;
    const auto curves = apvts.state.getChildWithName ("CURVES");

    if (curves.isValid())
    {
        const bool hasSlots = curves.getChildWithProperty ("index", 0).isValid()
                           || curves.getChildWithProperty ("index", 1).isValid();

        if (hasSlots)
        {
            for (int i = 0; i < 2; ++i)
            {
                const auto slotTree = curves.getChildWithProperty ("index", i);

                if (slotTree.isValid())
                {
                    readCurve (slotTree, "L", loaded[static_cast<std::size_t> (i)].left);
                    readCurve (slotTree, "R", loaded[static_cast<std::size_t> (i)].right);
                }
            }

            active = static_cast<int> (curves.getProperty ("active", 0));
        }
        else
        {
            // Legacy M2 state: L/R curves stored directly under CURVES.
            readCurve (curves, "L", loaded[0].left);
            readCurve (curves, "R", loaded[0].right);
            loaded[1] = loaded[0];
        }
    }

    if (active != 1)
        active = 0;

    {
        const juce::ScopedLock lock (curveLock);
        slots = loaded;
        activeSlot.store (active);
        writeCurvesToState();
    }
}

void PolarityAudioProcessor::setCurveSmoothing (int bins)
{
    const int clamped = juce::jlimit (0, 16, bins);

    if (curveSmoothingBins.load() == clamped)
        return;

    curveSmoothingBins.store (clamped);

    if (currentSampleRate.load() > 0.0)
        rebuildKernels();
}

void PolarityAudioProcessor::rebuildKernels()
{
    polarity::dsp::CurveModel left;
    polarity::dsp::CurveModel right;

    {
        const juce::ScopedLock lock (curveLock);
        const auto& slot = slots[static_cast<std::size_t> (activeSlot.load())];
        left = slot.left;
        right = slot.right;
    }

    const double sampleRate = currentSampleRate.load();

    if (sampleRate <= 0.0)
        return;

    const int edgeSmoothing = curveSmoothingBins.load (std::memory_order_relaxed);
    const int taps = getQualityTaps (qualityChoice.load (std::memory_order_relaxed));

    engine.setKernels (
        polarity::dsp::designKernel (left, sampleRate, taps, edgeSmoothing),
        polarity::dsp::designKernel (right, sampleRate, taps, edgeSmoothing));
}

int PolarityAudioProcessor::qualityFromState() const
{
    return juce::jlimit (0, getQualityChoiceCount() - 1,
                         static_cast<int> (apvts.state.getProperty ("quality", 1)));
}

void PolarityAudioProcessor::setQuality (int choice)
{
    choice = juce::jlimit (0, getQualityChoiceCount() - 1, choice);

    apvts.state.setProperty ("quality", choice, nullptr);

    if (qualityChoice.load (std::memory_order_relaxed) == choice)
        return;

    qualityChoice.store (choice, std::memory_order_relaxed);

    if (currentSampleRate.load (std::memory_order_relaxed) <= 0.0)
        return;

    applyQualityToEngine();
}

void PolarityAudioProcessor::applyQualityToEngine()
{
    const int taps = getQualityTaps (qualityChoice.load (std::memory_order_relaxed));

    activeLatency.store (taps / 2, std::memory_order_relaxed);
    setLatencySamples (taps / 2);
    rebuildKernels();
}

void PolarityAudioProcessor::resetAnalysis()
{
    std::fill (inputAnalysisRing.begin(), inputAnalysisRing.end(), 0.0f);
    std::fill (outputAnalysisRing.begin(), outputAnalysisRing.end(), 0.0f);
    inputAnalysisWrite = 0;
    outputAnalysisWrite = 0;

    for (auto& value : spectrumInput)
        value.store (-90.0f, std::memory_order_relaxed);

    for (auto& value : spectrumOutput)
        value.store (-90.0f, std::memory_order_relaxed);

    for (auto& value : waveformInputMin)
        value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : waveformInputMax)
        value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : waveformOutputMin)
        value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : waveformOutputMax)
        value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : waveformInputRms)
        value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : waveformOutputRms)
        value.store (0.0f, std::memory_order_relaxed);
}

void PolarityAudioProcessor::updateAnalysis (int wetLatency, int blockSamples)
{
    const int ringSize = static_cast<int> (inputAnalysisRing.size());
    const int outputSize = static_cast<int> (outputAnalysisRing.size());

    if (ringSize <= 0 || outputSize <= 0
        || analysisWork.size() < static_cast<std::size_t> (fftSize * 2))
        return;

    const double sampleRate = currentSampleRate.load();

    if (sampleRate <= 0.0)
        return;

    const int spectrumInputStart = ((inputAnalysisWrite - wetLatency - fftSize)
                                    % ringSize + ringSize) % ringSize;
    const int spectrumOutputStart = ((outputAnalysisWrite - fftSize)
                                     % outputSize + outputSize) % outputSize;

    const int windowSamples = getWaveformWindowSamples();
    const int waveformInputStart = ((inputAnalysisWrite - wetLatency - windowSamples)
                                    % ringSize + ringSize) % ringSize;
    const int waveformOutputStart = ((outputAnalysisWrite - windowSamples)
                                     % outputSize + outputSize) % outputSize;

    const double timeMs = static_cast<double> (
        spectrumTimeSmoothingMs.load (std::memory_order_relaxed));
    const double blockSeconds = static_cast<double> (blockSamples) / sampleRate;
    const float timeAlpha = timeMs > 0.0
                                ? static_cast<float> (
                                      juce::jlimit (0.001, 1.0,
                                                    1.0 - std::exp (-blockSeconds * 1000.0
                                                                    / timeMs)))
                                : 1.0f;

    const auto computeBands = [this, sampleRate, timeAlpha] (const std::vector<float>& ring,
                                                  int ringLength,
                                                  int start,
                                                  bool isInput)
    {
        std::fill (analysisWork.begin(), analysisWork.end(), 0.0f);

        for (int i = 0; i < fftSize; ++i)
            analysisWork[static_cast<std::size_t> (i)]
                = ring[static_cast<std::size_t> ((start + i) % ringLength)]
                  * hannWindow[static_cast<std::size_t> (i)];

        analysisFft.performFrequencyOnlyForwardTransform (analysisWork.data());

        std::array<float, spectrumBands> bandPower {};

        for (int band = 0; band < spectrumBands; ++band)
        {
            const double centerFreq = 20.0 * std::pow (
                1000.0, (static_cast<double> (band) + 0.5) / spectrumBands);
            const double position = centerFreq * fftSize / sampleRate;

            const int maxBin = fftSize / 2 - 1;
            int lowBin = std::max (1, static_cast<int> (
                                          std::floor (position)) - 2);
            int highBin = std::min (maxBin, static_cast<int> (
                                          std::ceil (position)) + 2);

            lowBin = std::min (lowBin, maxBin);

            if (highBin < lowBin)
                highBin = lowBin;

            const float normalise = static_cast<float> (fftSize) * 0.25f;
            float powerSum = 0.0f;
            float weightSum = 0.0f;

            for (int bin = lowBin; bin <= highBin; ++bin)
            {
                const float distance = std::abs (
                    static_cast<float> (bin) - static_cast<float> (position));
                const float weight = std::max (0.0f, 1.0f - distance / 3.0f);

                if (weight <= 0.0f)
                    continue;

                const float normalized = analysisWork[static_cast<std::size_t> (bin)]
                                         / normalise;
                powerSum += normalized * normalized * weight;
                weightSum += weight;
            }

            bandPower[static_cast<std::size_t> (band)]
                = weightSum > 0.0f ? powerSum / weightSum : 0.0f;
        }

        const int smoothingRadius = juce::jlimit (
            0, 16, spectrumSmoothingRadius.load (std::memory_order_relaxed));

        for (int band = 0; band < spectrumBands; ++band)
        {
            float sum = 0.0f;
            float weightSum = 0.0f;

            for (int k = -smoothingRadius; k <= smoothingRadius; ++k)
            {
                const int index = juce::jlimit (0, spectrumBands - 1, band + k);
                const float weight = 1.0f
                                     - static_cast<float> (std::abs (k))
                                           / static_cast<float> (smoothingRadius + 1);
                sum += bandPower[static_cast<std::size_t> (index)] * weight;
                weightSum += weight;
            }

            const float smoothedPower = sum / weightSum;
            const float smoothed = 10.0f * std::log10 (
                std::max (smoothedPower, 1.0e-14f));
            auto& target = isInput
                               ? spectrumInput[static_cast<std::size_t> (band)]
                               : spectrumOutput[static_cast<std::size_t> (band)];
            const float old = target.load (std::memory_order_relaxed);
            target.store (old * (1.0f - timeAlpha) + smoothed * timeAlpha,
                          std::memory_order_relaxed);
        }
    };

    computeBands (inputAnalysisRing, ringSize, spectrumInputStart, true);
    computeBands (outputAnalysisRing, outputSize, spectrumOutputStart, false);

    if ((waveformUpdateCounter++ % 4) == 0)
    {
    for (int i = 0; i < waveformPoints; ++i)
    {
        const int bucketStart = (i * windowSamples) / waveformPoints;
        const int bucketEnd = ((i + 1) * windowSamples) / waveformPoints;

        float inputMin = 0.0f;
        float inputMax = 0.0f;
        float outputMin = 0.0f;
        float outputMax = 0.0f;
        double inputSquares = 0.0;
        double outputSquares = 0.0;
        int count = 0;
        bool first = true;

        for (int j = bucketStart; j < bucketEnd; ++j)
        {
            const float inputValue =
                inputAnalysisRing[static_cast<std::size_t> (
                    (waveformInputStart + j) % ringSize)];
            const float outputValue =
                outputAnalysisRing[static_cast<std::size_t> (
                    (waveformOutputStart + j) % outputSize)];

            inputSquares += static_cast<double> (inputValue) * inputValue;
            outputSquares += static_cast<double> (outputValue) * outputValue;
            ++count;

            if (first)
            {
                inputMin = inputMax = inputValue;
                outputMin = outputMax = outputValue;
                first = false;
            }
            else
            {
                inputMin = std::min (inputMin, inputValue);
                inputMax = std::max (inputMax, inputValue);
                outputMin = std::min (outputMin, outputValue);
                outputMax = std::max (outputMax, outputValue);
            }
        }

        const float inputRms = count > 0
                                   ? static_cast<float> (
                                         std::sqrt (inputSquares / count))
                                   : 0.0f;
        const float outputRms = count > 0
                                    ? static_cast<float> (
                                          std::sqrt (outputSquares / count))
                                    : 0.0f;

        waveformInputMin[static_cast<std::size_t> (i)].store (
            inputMin, std::memory_order_relaxed);
        waveformInputMax[static_cast<std::size_t> (i)].store (
            inputMax, std::memory_order_relaxed);
        waveformOutputMin[static_cast<std::size_t> (i)].store (
            outputMin, std::memory_order_relaxed);
        waveformOutputMax[static_cast<std::size_t> (i)].store (
            outputMax, std::memory_order_relaxed);
        waveformInputRms[static_cast<std::size_t> (i)].store (
            inputRms, std::memory_order_relaxed);
        waveformOutputRms[static_cast<std::size_t> (i)].store (
            outputRms, std::memory_order_relaxed);
    }
    }
}

void PolarityAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void PolarityAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        loadCurvesFromState();
        qualityChoice.store (qualityFromState(), std::memory_order_relaxed);

        if (currentSampleRate.load (std::memory_order_relaxed) > 0.0)
        {
            const int taps = getQualityTaps (
                qualityChoice.load (std::memory_order_relaxed));
            activeLatency.store (taps / 2, std::memory_order_relaxed);
            setLatencySamples (taps / 2);
        }

        rebuildKernels();
        curveBroadcaster.sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolarityAudioProcessor();
}
