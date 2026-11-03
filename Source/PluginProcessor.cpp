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
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                return juce::jlimit (0.0f, 1.0f, text.getFloatValue() * 0.01f);
            })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 },
        "Bypass",
        false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool value, int) { return value ? "On" : "Off"; })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "deltaOut", 1 },
        "Delta Out",
        false,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction (
                [] (bool value, int) { return value ? "DELTA" : "NORM"; })
            .withValueFromStringFunction (
                [] (const juce::String& text)
                {
                    return text.trim().equalsIgnoreCase ("delta");
                })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "slot", 1 },
        "A/B Slot",
        juce::StringArray { "A", "B" },
        0,
        juce::AudioParameterChoiceAttributes().withStringFromValueFunction (
            [] (int index, int) { return index >= 1 ? "B" : "A"; })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "quality", 1 },
        "Quality",
        juce::StringArray { "2048 taps", "4096 taps", "8192 taps" },
        1,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "edge", 1 },
        "Edge Smoothing",
        juce::StringArray { "Off", "1 bin", "2 bins", "4 bins" },
        2));

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

    apvts.addParameterListener ("slot", this);
    apvts.addParameterListener ("quality", this);
    apvts.addParameterListener ("edge", this);

    writeCurvesToState();
    resetAnalysis();
}

PolarityAudioProcessor::~PolarityAudioProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener ("slot", this);
    apvts.removeParameterListener ("quality", this);
    apvts.removeParameterListener ("edge", this);
}

void PolarityAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);

    syncPendingFromParameters();

    const int newSlot = pendingSlot.load();
    const int quality = pendingQuality.load();
    const int edgeChoice = pendingEdgeChoice.load();

    {
        const juce::ScopedLock lock (curveLock);
        activeSlot.store (newSlot);
        writeCurvesToState();
    }

    qualityChoice.store (quality);
    curveSmoothingBins.store (getEdgeBinsForChoice (edgeChoice));

    const int taps = getQualityTaps (quality);

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

    dryLatencyFadeLength = juce::jmax (
        1, static_cast<int> (std::lround (0.06 * sampleRate)));
    dryLatencyInUse = taps / 2;
    dryLatencyPrevious = dryLatencyInUse;
    dryLatencyFadePos = dryLatencyFadeLength;

    prepareSpectrumMapping (sampleRate);

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
    spectrumUpdateCounter = 0;

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

    const float mix = mixParam != nullptr ? mixParam->load() : 1.0f;
    const bool bypassed = bypassParam != nullptr && bypassParam->load() >= 0.5f;
    const float deltaTarget = ! bypassed && deltaOutParam != nullptr
                                  && deltaOutParam->load() >= 0.5f
                              ? 1.0f : 0.0f;
    const bool analysisActive = editorRefCount.load (std::memory_order_relaxed) > 0;

    if (analysisActive && analysisResetRequested.exchange (false))
        resetAnalysis();

    const bool renderWet = ! bypassed && mix > 0.0f;
    engine.process (pointers, processedChannels, numSamples, renderWet);

    const int latency = juce::jlimit (
        0, static_cast<int> (dryDelay.empty() ? 0 : dryDelay[0].size()),
        activeLatency.load (std::memory_order_relaxed));

    if (latency != dryLatencyInUse)
    {
        dryLatencyPrevious = dryLatencyInUse;
        dryLatencyInUse = latency;
        dryLatencyFadePos = 0;
    }

    const bool latencyFading = dryLatencyFadePos < dryLatencyFadeLength
                               && dryLatencyPrevious != dryLatencyInUse;

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
        int readIndex = (storedIndex + delaySize - dryLatencyInUse) % delaySize;
        int previousIndex = latencyFading
                                ? (storedIndex + delaySize - dryLatencyPrevious) % delaySize
                                : readIndex;
        const auto* dryInput = dryCopy.getReadPointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float dry = delay[static_cast<std::size_t> (readIndex)];

            if (latencyFading)
            {
                const int position = juce::jlimit (
                    0, dryLatencyFadeLength, dryLatencyFadePos + sample);
                const float t = static_cast<float> (position)
                                / static_cast<float> (dryLatencyFadeLength);
                const float previousDry = delay[static_cast<std::size_t> (previousIndex)];
                dry = previousDry + (dry - previousDry) * t;

                previousIndex = (previousIndex + 1 == delaySize) ? 0 : previousIndex + 1;
            }

            delay[static_cast<std::size_t> (writeIndex)] = dryInput[sample];

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

    if (dryLatencyFadePos < dryLatencyFadeLength)
        dryLatencyFadePos = juce::jmin (dryLatencyFadeLength,
                                        dryLatencyFadePos + numSamples);

    if (! analysisActive)
        return;

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

        const double sampleRate = currentSampleRate.load (std::memory_order_relaxed);
        const double blockSeconds = sampleRate > 0.0
                                        ? static_cast<double> (numSamples) / sampleRate
                                        : 0.0;
        const float midAlpha = static_cast<float> (
            1.0 - std::exp (-blockSeconds / 0.040));
        const float correlationAlpha = static_cast<float> (
            1.0 - std::exp (-blockSeconds / 0.080));

        meterMidDb.store (std::clamp (
                              meterMidDb.load (std::memory_order_relaxed) * (1.0f - midAlpha)
                                  + midDb * midAlpha,
                              -72.0f, 6.0f),
                          std::memory_order_relaxed);
        meterSideDb.store (std::clamp (
                               meterSideDb.load (std::memory_order_relaxed) * (1.0f - midAlpha)
                                   + sideDb * midAlpha,
                               -72.0f, 6.0f),
                           std::memory_order_relaxed);
        meterCorrelation.store (std::clamp (
                                    meterCorrelation.load (std::memory_order_relaxed)
                                        * (1.0f - correlationAlpha)
                                        + static_cast<float> (correlation) * correlationAlpha,
                                    -1.0f, 1.0f),
                                std::memory_order_relaxed);
    }

    // --- analysis feeds (input aligned to the wet path's latency) --------
    {
        const int ringSize = static_cast<int> (inputAnalysisRing.size());
        const int outputSize = static_cast<int> (outputAnalysisRing.size());

        if (ringSize > 0 && outputSize > 0)
        {
            const auto* dryLeft = dryCopy.getReadPointer (0);
            const auto* dryRight = processedChannels > 1 ? dryCopy.getReadPointer (1)
                                                         : dryLeft;
            const auto* outLeft = buffer.getReadPointer (0);
            const auto* outRight = processedChannels > 1 ? buffer.getReadPointer (1)
                                                         : outLeft;

            for (int i = 0; i < numSamples; ++i)
            {
                inputAnalysisRing[static_cast<std::size_t> (inputAnalysisWrite)]
                    = 0.5f * (dryLeft[i] + dryRight[i]);
                inputAnalysisWrite = (inputAnalysisWrite + 1) % ringSize;

                outputAnalysisRing[static_cast<std::size_t> (outputAnalysisWrite)]
                    = 0.5f * (outLeft[i] + outRight[i]);
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

    pendingSlot.store (slot);
    writeParameterValue ("slot", slot);
    scheduleSettingApply();
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
        }
        else
        {
            // Legacy M2 state: L/R curves stored directly under CURVES.
            readCurve (curves, "L", loaded[0].left);
            readCurve (curves, "R", loaded[0].right);
            loaded[1] = loaded[0];
        }
    }

    {
        const juce::ScopedLock lock (curveLock);
        slots = loaded;
    }
}

void PolarityAudioProcessor::setCurveSmoothing (int bins)
{
    const int choice = getEdgeChoiceForBins (juce::jlimit (0, 16, bins));

    pendingEdgeChoice.store (choice);
    writeParameterValue ("edge", choice);
    scheduleSettingApply();
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

void PolarityAudioProcessor::setQuality (int choice)
{
    choice = juce::jlimit (0, getQualityChoiceCount() - 1, choice);

    pendingQuality.store (choice);
    writeParameterValue ("quality", choice);
    scheduleSettingApply();
}

int PolarityAudioProcessor::parameterIndex (const juce::String& id, int fallback) const
{
    if (auto* parameter = apvts.getParameter (id))
        return juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));

    return fallback;
}

void PolarityAudioProcessor::writeParameterValue (const juce::String& id, int value)
{
    if (auto* parameter = apvts.getParameter (id))
    {
        const auto normalised = parameter->convertTo0to1 (static_cast<float> (value));

        if (! juce::approximatelyEqual (parameter->getValue(), normalised))
            parameter->setValueNotifyingHost (normalised);
    }
}

void PolarityAudioProcessor::syncPendingFromParameters()
{
    pendingSlot.store (juce::jlimit (0, 1, parameterIndex ("slot", 0)));
    pendingQuality.store (juce::jlimit (0, getQualityChoiceCount() - 1,
                                        parameterIndex ("quality", 1)));
    pendingEdgeChoice.store (juce::jlimit (0, getEdgeChoiceCount() - 1,
                                           parameterIndex ("edge", 2)));
}

void PolarityAudioProcessor::scheduleSettingApply()
{
    if (juce::MessageManager::existsAndIsCurrentThread())
        applyPendingSettings();
    else
        triggerAsyncUpdate();
}

void PolarityAudioProcessor::parameterChanged (const juce::String& parameterID,
                                               float newValue)
{
    if (loadingState.load (std::memory_order_acquire))
        return;

    if (parameterID == "slot")
    {
        const int value = juce::jlimit (0, 1, juce::roundToInt (newValue));

        if (pendingSlot.exchange (value) != value)
            triggerAsyncUpdate();
    }
    else if (parameterID == "quality")
    {
        const int value = juce::jlimit (0, getQualityChoiceCount() - 1,
                                        juce::roundToInt (newValue));

        if (pendingQuality.exchange (value) != value)
            triggerAsyncUpdate();
    }
    else if (parameterID == "edge")
    {
        const int value = juce::jlimit (0, getEdgeChoiceCount() - 1,
                                        juce::roundToInt (newValue));

        if (pendingEdgeChoice.exchange (value) != value)
            triggerAsyncUpdate();
    }
}

void PolarityAudioProcessor::handleAsyncUpdate()
{
    applyPendingSettings();
}

void PolarityAudioProcessor::applyPendingSettings()
{
    const int newSlot = juce::jlimit (0, 1, pendingSlot.load());
    const int newQuality = juce::jlimit (0, getQualityChoiceCount() - 1,
                                         pendingQuality.load());
    const int newEdgeBins = getEdgeBinsForChoice (pendingEdgeChoice.load());

    const bool slotChanged = activeSlot.load() != newSlot;
    const bool qualityChanged = qualityChoice.load() != newQuality;
    const bool edgeChanged = curveSmoothingBins.load() != newEdgeBins;

    if (! slotChanged && ! qualityChanged && ! edgeChanged)
        return;

    if (slotChanged)
    {
        const juce::ScopedLock lock (curveLock);
        activeSlot.store (newSlot);
        writeCurvesToState();
    }

    if (qualityChanged)
    {
        qualityChoice.store (newQuality);

        if (currentSampleRate.load (std::memory_order_relaxed) > 0.0)
        {
            const int latency = getQualityTaps (newQuality) / 2;
            activeLatency.store (latency, std::memory_order_relaxed);
            setLatencySamples (latency);
        }
    }

    if (edgeChanged)
        curveSmoothingBins.store (newEdgeBins);

    if (currentSampleRate.load (std::memory_order_relaxed) > 0.0)
        rebuildKernels();

    if (slotChanged)
        curveBroadcaster.sendChangeMessage();
}

void PolarityAudioProcessor::flushPendingParameterChanges()
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());
    handleUpdateNowIfNeeded();
}

void PolarityAudioProcessor::setEditorActive (bool active)
{
    if (active)
    {
        if (editorRefCount.fetch_add (1, std::memory_order_relaxed) == 0)
            analysisResetRequested.store (true, std::memory_order_relaxed);
    }
    else
    {
        int current = editorRefCount.load (std::memory_order_relaxed);

        while (current > 0
               && ! editorRefCount.compare_exchange_weak (current, current - 1,
                                                          std::memory_order_relaxed))
        {
        }
    }
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

    meterMidDb.store (-72.0f, std::memory_order_relaxed);
    meterSideDb.store (-72.0f, std::memory_order_relaxed);
    meterCorrelation.store (0.0f, std::memory_order_relaxed);
}

void PolarityAudioProcessor::prepareSpectrumMapping (double sampleRate)
{
    const int maxBin = fftSize / 2 - 1;

    for (int band = 0; band < spectrumBands; ++band)
    {
        const double centerFreq = 20.0 * std::pow (
            1000.0, (static_cast<double> (band) + 0.5) / spectrumBands);
        const double position = centerFreq * fftSize / sampleRate;

        int lowBin = std::max (1, static_cast<int> (std::floor (position)) - 2);
        int highBin = std::min (maxBin, static_cast<int> (std::ceil (position)) + 2);
        lowBin = std::min (lowBin, maxBin);

        if (highBin < lowBin)
            highBin = lowBin;

        int count = 0;
        float weightSum = 0.0f;

        for (int bin = lowBin; bin <= highBin && count < maxBandBins; ++bin)
        {
            const float distance = std::abs (
                static_cast<float> (bin) - static_cast<float> (position));
            const float weight = std::max (0.0f, 1.0f - distance / 3.0f);

            if (weight <= 0.0f)
                continue;

            bandBinIndex[static_cast<std::size_t> (band)]
                        [static_cast<std::size_t> (count)] = bin;
            bandBinWeight[static_cast<std::size_t> (band)]
                         [static_cast<std::size_t> (count)] = weight;
            ++count;
            weightSum += weight;
        }

        bandBinCount[static_cast<std::size_t> (band)] = count;
        bandWeightSum[static_cast<std::size_t> (band)] = weightSum;
    }
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

    // The FFT pair is the expensive part; ~30 Hz is plenty for a display and
    // avoids doing two 16384-point transforms per audio block.
    const int blocksPerSpectrumUpdate = juce::jmax (
        1, juce::roundToInt (sampleRate
                             / (30.0 * static_cast<double> (blockSamples))));
    const bool updateSpectrum = (spectrumUpdateCounter++ % blocksPerSpectrumUpdate) == 0;

    const double timeMs = static_cast<double> (
        spectrumTimeSmoothingMs.load (std::memory_order_relaxed));
    const double blockSeconds = static_cast<double> (blockSamples) / sampleRate;
    const double updateSeconds = blockSeconds * blocksPerSpectrumUpdate;
    const float timeAlpha = timeMs > 0.0
                                ? static_cast<float> (
                                      juce::jlimit (0.001, 1.0,
                                                    1.0 - std::exp (-updateSeconds * 1000.0
                                                                    / timeMs)))
                                : 1.0f;

    const auto computeBands = [this, timeAlpha] (const std::vector<float>& ring,
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
        const float normalise = static_cast<float> (fftSize) * 0.25f;

        for (int band = 0; band < spectrumBands; ++band)
        {
            const int count = bandBinCount[static_cast<std::size_t> (band)];
            const float weightSum = bandWeightSum[static_cast<std::size_t> (band)];
            float powerSum = 0.0f;

            for (int k = 0; k < count; ++k)
            {
                const int bin = bandBinIndex[static_cast<std::size_t> (band)]
                                            [static_cast<std::size_t> (k)];
                const float weight = bandBinWeight[static_cast<std::size_t> (band)]
                                                  [static_cast<std::size_t> (k)];
                const float normalized = analysisWork[static_cast<std::size_t> (bin)]
                                         / normalise;
                powerSum += normalized * normalized * weight;
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

    if (updateSpectrum)
    {
        computeBands (inputAnalysisRing, ringSize, spectrumInputStart, true);
        computeBands (outputAnalysisRing, outputSize, spectrumOutputStart, false);
    }

    // Match the waveform panel's 30 Hz repaint; a lower analysis rate makes
    // the trace visibly step even though the panel keeps redrawing.
    const int blocksPerWaveformUpdate = juce::jmax (
        1, juce::roundToInt (sampleRate
                             / (30.0 * static_cast<double> (blockSamples))));

    if ((waveformUpdateCounter++ % blocksPerWaveformUpdate) == 0)
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
    auto state = apvts.copyState();

    // The slot parameter is authoritative; patch the copied curve tree so a
    // state saved while an async apply is pending stays self-consistent.
    const int slot = parameterIndex ("slot", activeSlot.load());
    auto curves = state.getChildWithName ("CURVES");

    if (curves.isValid())
        curves.setProperty ("active", slot, nullptr);

    state.setProperty ("stateVersion", getStateVersion(), nullptr);

    const std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void PolarityAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // Version 0 predates the slot / quality parameters and the schema marker.
    const int sourceVersion = xml->getIntAttribute ("stateVersion", 0);
    const bool hasSlotParameter = xml->getChildByAttribute ("id", "slot") != nullptr;
    const bool hasQualityParameter = xml->getChildByAttribute ("id", "quality") != nullptr;

    int legacySlot = 0;

    if (auto* curves = xml->getChildByName ("CURVES"))
        legacySlot = juce::jlimit (0, 1, curves->getIntAttribute ("active", 0));

    const int legacyQuality = juce::jlimit (0, getQualityChoiceCount() - 1,
                                            xml->getIntAttribute ("quality", 1));

    loadingState.store (true, std::memory_order_release);
    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    if (! hasSlotParameter)
        writeParameterValue ("slot", legacySlot);

    // A versioned state's root "quality" property is stale by definition.
    if (! hasQualityParameter && sourceVersion == 0)
        writeParameterValue ("quality", legacyQuality);

    loadingState.store (false, std::memory_order_release);

    const int slot = hasSlotParameter ? parameterIndex ("slot", legacySlot)
                                      : legacySlot;
    const int quality = hasQualityParameter ? parameterIndex ("quality", legacyQuality)
                                            : legacyQuality;
    const int edgeChoice = parameterIndex ("edge", 2);
    const int edgeBins = getEdgeBinsForChoice (edgeChoice);

    pendingSlot.store (slot);
    pendingQuality.store (quality);
    pendingEdgeChoice.store (edgeChoice);

    loadCurvesFromState();

    {
        const juce::ScopedLock lock (curveLock);
        activeSlot.store (slot);
        qualityChoice.store (quality);
        curveSmoothingBins.store (edgeBins);
        writeCurvesToState();
        apvts.state.removeProperty ("quality", nullptr);
        apvts.state.setProperty ("stateVersion", getStateVersion(), nullptr);
    }

    if (currentSampleRate.load (std::memory_order_relaxed) > 0.0)
    {
        const int latency = getQualityTaps (quality) / 2;
        activeLatency.store (latency, std::memory_order_relaxed);
        setLatencySamples (latency);
    }

    rebuildKernels();
    curveBroadcaster.sendChangeMessage();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolarityAudioProcessor();
}
