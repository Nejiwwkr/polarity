#include "PluginProcessor.h"
#include "state/PresetManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kSampleRate = 48000.0;
constexpr int blockSize = 512;

struct AudioConfig
{
    double sampleRate = kSampleRate;
    int blockSize = ::blockSize;
};

using polarity::dsp::CurvePoint;

std::vector<CurvePoint> flatCurve()
{
    return { { 20.0, 0.0 }, { 20000.0, 0.0 } };
}

std::vector<CurvePoint> lowNullCurve()
{
    return { { 20.0, 0.0 },
             { 100.0, 0.0 },
             { 200.0, 1.0 },
             { 2000.0, 1.0 },
             { 4000.0, 0.0 },
             { 20000.0, 0.0 } };
}

std::vector<CurvePoint> highNullCurve()
{
    return { { 20.0, 0.0 },
             { 2000.0, 0.0 },
             { 3000.0, 1.0 },
             { 8000.0, 1.0 },
             { 12000.0, 0.0 },
             { 20000.0, 0.0 } };
}

/** Wide transitions survive the coarser bin spacing at 192 kHz. */
std::vector<CurvePoint> wideNullCurve()
{
    return { { 20.0, 0.0 },
             { 100.0, 0.0 },
             { 500.0, 1.0 },
             { 5000.0, 1.0 },
             { 10000.0, 0.0 },
             { 20000.0, 0.0 } };
}

bool hasPointNear (const std::vector<CurvePoint>& points,
                   double freq,
                   double amount,
                   double tolerance = 1.0)
{
    for (const auto& point : points)
        if (std::abs (point.freqHz - freq) <= tolerance
            && std::abs (point.amount - amount) <= 0.05)
            return true;

    return false;
}

int bandForFreq (double freq)
{
    const int bands = PolarityAudioProcessor::getSpectrumBandCount();
    const double t = std::log (freq / 20.0) / std::log (1000.0);

    return juce::jlimit (0, bands - 1,
                         static_cast<int> (std::round (t * bands)));
}

void fillSine (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
               double freq, double sampleRate)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (channel, i,
                              0.5f * static_cast<float> (std::sin (
                                  2.0 * kPi * freq
                                  * static_cast<double> (startSample + i) / sampleRate)));
}

void fillSine (juce::AudioBuffer<float>& buffer, int startSample, double freq)
{
    fillSine (buffer, startSample, buffer.getNumSamples(), freq, kSampleRate);
}

double measureSineGainDb (PolarityAudioProcessor& processor,
                          double freq,
                          int channel,
                          AudioConfig cfg = {},
                          double seconds = 1.0)
{
    processor.prepareToPlay (cfg.sampleRate, cfg.blockSize);

    const int channels = juce::jmax (1, processor.getTotalNumInputChannels());
    juce::AudioBuffer<float> buffer (channels, cfg.blockSize);
    juce::MidiBuffer midi;
    const int total = static_cast<int> (cfg.sampleRate * seconds);
    double sum = 0.0;
    int count = 0;

    for (int position = 0; position < total; position += cfg.blockSize)
    {
        const int n = std::min (cfg.blockSize, total - position);
        fillSine (buffer, position, n, freq, cfg.sampleRate);

        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), channels, n);
        processor.processBlock (view, midi);

        if (position >= total / 2)
        {
            for (int i = 0; i < n; ++i)
            {
                const double value = view.getSample (channel, i);
                sum += value * value;
                ++count;
            }
        }
    }

    const double rms = std::sqrt (sum / static_cast<double> (count));
    return 20.0 * std::log10 (std::max (rms * std::sqrt (2.0) / 0.5, 1.0e-15));
}

double measureDelayedDryError (PolarityAudioProcessor& processor,
                               float mix,
                               bool bypassed,
                               AudioConfig cfg = {},
                               double seconds = 1.0)
{
    auto* mixParameter = processor.getValueTreeState().getParameter ("mix");
    auto* bypassParameter = processor.getValueTreeState().getParameter ("bypass");

    if (mixParameter == nullptr || bypassParameter == nullptr)
        return 1.0e9;

    mixParameter->setValueNotifyingHost (mix);
    bypassParameter->setValueNotifyingHost (bypassed ? 1.0f : 0.0f);

    if (auto* delta = processor.getValueTreeState().getParameter ("deltaOut"))
        delta->setValueNotifyingHost (0.0f);

    processor.prepareToPlay (cfg.sampleRate, cfg.blockSize);

    const int channels = juce::jmax (1, processor.getTotalNumInputChannels());
    const int total = static_cast<int> (cfg.sampleRate * seconds);
    juce::AudioBuffer<float> buffer (channels, cfg.blockSize);
    juce::MidiBuffer midi;

    std::mt19937 rng (3);
    std::normal_distribution<float> noise (0.0f, 0.1f);

    std::vector<float> input (static_cast<std::size_t> (total));
    std::vector<float> output (static_cast<std::size_t> (total));

    for (auto& value : input)
        value = noise (rng);

    for (int position = 0; position < total; position += cfg.blockSize)
    {
        const int count = std::min (cfg.blockSize, total - position);

        for (int channel = 0; channel < channels; ++channel)
            for (int i = 0; i < count; ++i)
                buffer.setSample (channel, i,
                                  input[static_cast<std::size_t> (position + i)]);

        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), channels, count);
        processor.processBlock (view, midi);

        for (int i = 0; i < count; ++i)
            output[static_cast<std::size_t> (position + i)]
                = view.getSample (0, i);
    }

    const int latency = processor.getLatencySamples();
    double error = 0.0;

    for (int i = 0; i < total - latency; ++i)
        error = std::max (error,
                          std::abs (static_cast<double> (output[static_cast<std::size_t> (i + latency)])
                                    - static_cast<double> (input[static_cast<std::size_t> (i)])));

    return error;
}

double measureCrossfadeClick (PolarityAudioProcessor& processor,
                              double& tailDb,
                              AudioConfig cfg = {})
{
    if (auto* mix = processor.getValueTreeState().getParameter ("mix"))
        mix->setValueNotifyingHost (1.0f);

    if (auto* bypass = processor.getValueTreeState().getParameter ("bypass"))
        bypass->setValueNotifyingHost (0.0f);

    if (auto* delta = processor.getValueTreeState().getParameter ("deltaOut"))
        delta->setValueNotifyingHost (0.0f);

    processor.setCurvePoints (0, flatCurve());
    processor.setCurvePoints (1, flatCurve());
    processor.prepareToPlay (cfg.sampleRate, cfg.blockSize);

    juce::AudioBuffer<float> buffer (2, cfg.blockSize);
    juce::MidiBuffer midi;
    const int total = static_cast<int> (cfg.sampleRate);
    double previous = 0.0;
    double maxStep = 0.0;
    double tailSum = 0.0;
    int tailCount = 0;
    bool changed = false;

    for (int position = 0; position < total; position += cfg.blockSize)
    {
        const int n = std::min (cfg.blockSize, total - position);
        fillSine (buffer, position, n, 1000.0, cfg.sampleRate);

        if (! changed && position >= static_cast<int> (cfg.sampleRate * 0.25))
        {
            processor.setCurvePoints (0, lowNullCurve());
            processor.setCurvePoints (1, lowNullCurve());
            changed = true;
        }

        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        processor.processBlock (view, midi);

        for (int i = 0; i < n; ++i)
        {
            const double value = view.getSample (0, i);

            if (position + i > 0)
                maxStep = std::max (maxStep, std::abs (value - previous));

            previous = value;

            if (position + i >= static_cast<int> (cfg.sampleRate * 0.6))
            {
                tailSum += value * value;
                ++tailCount;
            }
        }
    }

    tailDb = 20.0 * std::log10 (
        std::max (std::sqrt (tailSum / static_cast<double> (tailCount))
                      * std::sqrt (2.0) / 0.5,
                  1.0e-15));

    return maxStep;
}

struct MeterResult
{
    float midDb = -72.0f;
    float sideDb = -72.0f;
    float correlation = 0.0f;
};

MeterResult measureMeters (PolarityAudioProcessor& processor, bool antiPhase)
{
    if (auto* mix = processor.getValueTreeState().getParameter ("mix"))
        mix->setValueNotifyingHost (1.0f);

    if (auto* bypass = processor.getValueTreeState().getParameter ("bypass"))
        bypass->setValueNotifyingHost (0.0f);

    processor.setCurves (flatCurve(), flatCurve());
    processor.prepareToPlay (kSampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int position = 0; position < static_cast<int> (kSampleRate * 0.5);
         position += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float value = 0.5f * static_cast<float> (
                std::sin (2.0 * kPi * 1000.0
                          * static_cast<double> (position + i) / kSampleRate));

            buffer.setSample (0, i, value);
            buffer.setSample (1, i, antiPhase ? -value : value);
        }

        processor.processBlock (buffer, midi);
    }

    return { processor.getMeterMidDb(),
             processor.getMeterSideDb(),
             processor.getMeterCorrelation() };
}

bool testABSlots (PolarityAudioProcessor& processor)
{
    processor.setActiveSlot (0);
    processor.setCurvePoints (0, lowNullCurve());
    processor.setCurvePoints (1, lowNullCurve());

    processor.setActiveSlot (1);
    processor.setCurvePoints (0, highNullCurve());
    processor.setCurvePoints (1, highNullCurve());

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    processor.setActiveSlot (0);

    const bool slotAOk = processor.getActiveSlot() == 0
                      && hasPointNear (processor.getCurvePoints (0), 200.0, 1.0)
                      && hasPointNear (processor.getCurvePoints (1), 2000.0, 1.0);

    processor.setStateInformation (state.getData(),
                                   static_cast<int> (state.getSize()));

    const bool slotBOk = processor.getActiveSlot() == 1
                      && hasPointNear (processor.getCurvePoints (0), 3000.0, 1.0)
                      && hasPointNear (processor.getCurvePoints (1), 8000.0, 1.0);

    return slotAOk && slotBOk;
}

bool testPresets (PolarityAudioProcessor& processor)
{
    const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("PolarityTests");

    if (! base.isDirectory() && base.createDirectory().failed())
        return false;

    const auto directory = base.getChildFile (
        juce::String (juce::Time::currentTimeMillis()));

    if (! directory.isDirectory() && directory.createDirectory().failed())
        return false;

    PresetManager manager (processor, directory);

    processor.setActiveSlot (0);
    processor.setCurvePoints (0, lowNullCurve());
    processor.setCurvePoints (1, lowNullCurve());

    const auto name = PresetManager::sanitiseName ("Round Trip");
    const bool saved = manager.savePreset (name);

    processor.setCurvePoints (0, highNullCurve());
    processor.setCurvePoints (1, highNullCurve());

    const bool loaded = manager.loadPreset (name);
    const bool restored = hasPointNear (processor.getCurvePoints (0), 200.0, 1.0);
    const bool listed = manager.getPresetNames().contains (name);

    directory.deleteRecursively();
    return saved && loaded && restored && listed;
}

struct AnalysisResult
{
    float inFlat = -90.0f;
    float outFlat = -90.0f;
    float inNull = -90.0f;
    float outNull = -90.0f;
    float maxWave = 0.0f;
    bool ok = false;
};

AnalysisResult testAnalysis (PolarityAudioProcessor& processor,
                             AudioConfig cfg = {})
{
    if (auto* mix = processor.getValueTreeState().getParameter ("mix"))
        mix->setValueNotifyingHost (1.0f);

    if (auto* bypass = processor.getValueTreeState().getParameter ("bypass"))
        bypass->setValueNotifyingHost (0.0f);

    processor.setActiveSlot (0);
    processor.setCurves (flatCurve(), flatCurve());
    processor.prepareToPlay (cfg.sampleRate, cfg.blockSize);
    processor.setWaveformWindowMs (200.0f);

    juce::AudioBuffer<float> buffer (2, cfg.blockSize);
    juce::MidiBuffer midi;

    const auto runSine = [&] (double seconds)
    {
        const int total = static_cast<int> (cfg.sampleRate * seconds);

        for (int position = 0; position < total; position += cfg.blockSize)
        {
            const int n = std::min (cfg.blockSize, total - position);

            for (int i = 0; i < n; ++i)
            {
                const float value = 0.5f * static_cast<float> (
                    std::sin (2.0 * kPi * 1000.0
                              * static_cast<double> (position + i) / cfg.sampleRate));

                buffer.setSample (0, i, value);
                buffer.setSample (1, i, value);
            }

            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
            processor.processBlock (view, midi);
        }
    };

    runSine (1.2);

    const int band = bandForFreq (1000.0);
    AnalysisResult result;
    result.inFlat = processor.getSpectrumInput (band);
    result.outFlat = processor.getSpectrumOutput (band);

    processor.setCurvePoints (0, lowNullCurve());
    processor.setCurvePoints (1, lowNullCurve());
    runSine (1.2);

    result.inNull = processor.getSpectrumInput (band);
    result.outNull = processor.getSpectrumOutput (band);

    for (int i = 0; i < PolarityAudioProcessor::getWaveformPointCount(); ++i)
    {
        result.maxWave = std::max (result.maxWave,
                                   std::abs (processor.getWaveformOutputMin (i)));
        result.maxWave = std::max (result.maxWave,
                                   std::abs (processor.getWaveformOutputMax (i)));
    }

    result.ok = result.inFlat > -40.0f
             && result.outFlat > -40.0f
             && result.inNull > -40.0f
             && result.inFlat - result.inNull < 6.0f
             && result.outFlat - result.outNull > 20.0f
             && result.maxWave < 0.05f;

    return result;
}

std::vector<CurvePoint> invertCurve()
{
    return { { 20.0, 0.0 },
             { 100.0, 0.0 },
             { 200.0, 2.0 },
             { 2000.0, 2.0 },
             { 4000.0, 0.0 },
             { 20000.0, 0.0 } };
}

double measureDeltaCrossfade (PolarityAudioProcessor& processor)
{
    auto* delta = processor.getValueTreeState().getParameter ("deltaOut");
    auto* mix = processor.getValueTreeState().getParameter ("mix");
    auto* bypass = processor.getValueTreeState().getParameter ("bypass");

    if (delta == nullptr || mix == nullptr || bypass == nullptr)
        return 1.0e9;

    delta->setValueNotifyingHost (0.0f);
    mix->setValueNotifyingHost (1.0f);
    bypass->setValueNotifyingHost (0.0f);
    processor.setCurves (lowNullCurve(), flatCurve());
    processor.prepareToPlay (kSampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double previous = 0.0;
    double maxStep = 0.0;
    bool changed = false;

    for (int position = 0; position < static_cast<int> (kSampleRate); position += blockSize)
    {
        fillSine (buffer, position, 1000.0);

        if (! changed && position >= static_cast<int> (kSampleRate * 0.25))
        {
            delta->setValueNotifyingHost (1.0f);
            changed = true;
        }

        processor.processBlock (buffer, midi);

        for (int i = 0; i < blockSize; ++i)
        {
            const double value = buffer.getSample (0, i);

            if (position + i > 0)
                maxStep = std::max (maxStep, std::abs (value - previous));

            previous = value;
        }
    }

    delta->setValueNotifyingHost (0.0f);
    return maxStep;
}

bool testDeltaMode (PolarityAudioProcessor& processor)
{
    auto* delta = processor.getValueTreeState().getParameter ("deltaOut");
    auto* mix = processor.getValueTreeState().getParameter ("mix");
    auto* bypass = processor.getValueTreeState().getParameter ("bypass");

    if (delta == nullptr || mix == nullptr || bypass == nullptr)
        return false;

    processor.setActiveSlot (0);
    processor.setCurves (lowNullCurve(), flatCurve());
    mix->setValueNotifyingHost (1.0f);
    bypass->setValueNotifyingHost (0.0f);
    delta->setValueNotifyingHost (0.0f);

    const double normalL = measureSineGainDb (processor, 400.0, 0);
    const double normalR = measureSineGainDb (processor, 400.0, 1);

    delta->setValueNotifyingHost (1.0f);
    const double deltaL = measureSineGainDb (processor, 400.0, 0);
    const double deltaR = measureSineGainDb (processor, 400.0, 1);

    // A=2 in the band: delta output is 2x the band content (+6 dB).
    processor.setCurves (invertCurve(), flatCurve());
    const double invertDeltaL = measureSineGainDb (processor, 400.0, 0);

    // mix=0: no action, delta is silent.
    processor.setCurves (lowNullCurve(), flatCurve());
    mix->setValueNotifyingHost (0.0f);
    const double mixZeroDeltaL = measureSineGainDb (processor, 400.0, 0);

    // bypass wins over delta.
    mix->setValueNotifyingHost (1.0f);
    bypass->setValueNotifyingHost (1.0f);
    const double bypassDeltaL = measureSineGainDb (processor, 400.0, 0);

    // Back to NORM: the null returns.
    bypass->setValueNotifyingHost (0.0f);
    delta->setValueNotifyingHost (0.0f);
    const double restoredL = measureSineGainDb (processor, 400.0, 0);

    const double crossfadeStep = measureDeltaCrossfade (processor);

    std::printf ("delta normal L/R : %.1f / %.1f dB\n", normalL, normalR);
    std::printf ("delta L/R        : %.1f / %.1f dB\n", deltaL, deltaR);
    std::printf ("delta A=2 L      : %.1f dB\n", invertDeltaL);
    std::printf ("delta mix=0 L    : %.1f dB\n", mixZeroDeltaL);
    std::printf ("delta bypass L   : %.1f dB\n", bypassDeltaL);
    std::printf ("delta restored L : %.1f dB\n", restoredL);
    std::printf ("delta fade max   : %.4f\n", crossfadeStep);

    return normalL < -40.0
        && std::abs (normalR) < 0.1
        && std::abs (deltaL) < 0.5
        && deltaR < -60.0
        && invertDeltaL > 5.0 && invertDeltaL < 7.0
        && mixZeroDeltaL < -60.0
        && std::abs (bypassDeltaL) < 0.1
        && restoredL < -40.0
        && crossfadeStep < 0.15;
}

struct QualityResult
{
    int latency = 0;
    double nullDb = 0.0;
    double passDb = 0.0;
    double deltaFlatDb = 0.0;
    double dryError = 0.0;
};

bool testQuality (PolarityAudioProcessor& processor)
{
    auto* mix = processor.getValueTreeState().getParameter ("mix");
    auto* bypass = processor.getValueTreeState().getParameter ("bypass");
    auto* delta = processor.getValueTreeState().getParameter ("deltaOut");

    if (mix == nullptr || bypass == nullptr || delta == nullptr)
        return false;

    processor.setActiveSlot (0);
    mix->setValueNotifyingHost (1.0f);
    bypass->setValueNotifyingHost (0.0f);
    delta->setValueNotifyingHost (0.0f);

    const auto run = [&] (int quality)
    {
        processor.setCurves (lowNullCurve(), flatCurve());
        processor.setQuality (quality);
        processor.prepareToPlay (kSampleRate, blockSize);

        QualityResult result;
        result.latency = processor.getLatencySamples();
        result.nullDb = measureSineGainDb (processor, 400.0, 0);
        result.passDb = measureSineGainDb (processor, 400.0, 1);

        // Flat R curve in DELTA: dry - wet must be silent, which proves the
        // variable-length kernel is aligned with the dry delay.
        delta->setValueNotifyingHost (1.0f);
        result.deltaFlatDb = measureSineGainDb (processor, 400.0, 1);
        delta->setValueNotifyingHost (0.0f);

        bypass->setValueNotifyingHost (1.0f);
        result.dryError = measureDelayedDryError (processor, 1.0f, true);
        bypass->setValueNotifyingHost (0.0f);
        return result;
    };

    const auto low = run (0);
    const auto normal = run (1);
    const auto high = run (2);

    auto* qualityParameter = processor.getValueTreeState().getParameter ("quality");
    const int storedQuality = qualityParameter != nullptr
                                  ? juce::roundToInt (qualityParameter->convertFrom0to1 (
                                        qualityParameter->getValue()))
                                  : -1;

    processor.setQuality (1);
    processor.prepareToPlay (kSampleRate, blockSize);
    const int restoredLatency = processor.getLatencySamples();

    std::printf ("quality 2048     : lat %d  null %.1f dB  delta-flat %.1f dB  dry %.1e\n",
                 low.latency, low.nullDb, low.deltaFlatDb, low.dryError);
    std::printf ("quality 4096     : lat %d  null %.1f dB  delta-flat %.1f dB  dry %.1e\n",
                 normal.latency, normal.nullDb, normal.deltaFlatDb, normal.dryError);
    std::printf ("quality 8192     : lat %d  null %.1f dB  delta-flat %.1f dB  dry %.1e\n",
                 high.latency, high.nullDb, high.deltaFlatDb, high.dryError);
    std::printf ("quality restore  : %d (stored %d)\n",
                 restoredLatency, storedQuality);

    return low.latency == 1024
        && normal.latency == 2048
        && high.latency == 4096
        && low.nullDb < -20.0
        && normal.nullDb < -40.0
        && high.nullDb < -40.0
        && std::abs (low.passDb) < 0.1
        && std::abs (normal.passDb) < 0.1
        && std::abs (high.passDb) < 0.1
        && low.deltaFlatDb < -60.0
        && normal.deltaFlatDb < -60.0
        && high.deltaFlatDb < -60.0
        && low.dryError < 1.0e-6
        && normal.dryError < 1.0e-6
        && high.dryError < 1.0e-6
        && storedQuality == 2
        && restoredLatency == 2048;
}

double benchmarkProcessing (PolarityAudioProcessor& processor, double seconds,
                            int quality = 1, AudioConfig cfg = {})
{
    if (auto* mix = processor.getValueTreeState().getParameter ("mix"))
        mix->setValueNotifyingHost (1.0f);

    if (auto* bypass = processor.getValueTreeState().getParameter ("bypass"))
        bypass->setValueNotifyingHost (0.0f);

    if (auto* delta = processor.getValueTreeState().getParameter ("deltaOut"))
        delta->setValueNotifyingHost (0.0f);

    processor.setCurves (lowNullCurve(), flatCurve());
    processor.setQuality (quality);
    processor.prepareToPlay (cfg.sampleRate, cfg.blockSize);

    std::mt19937 rng (11);
    std::normal_distribution<float> noise (0.0f, 0.1f);
    juce::AudioBuffer<float> source (2, cfg.blockSize);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < cfg.blockSize; ++i)
            source.setSample (channel, i, noise (rng));

    juce::AudioBuffer<float> buffer (2, cfg.blockSize);
    juce::MidiBuffer midi;
    const int blocks = static_cast<int> (seconds * cfg.sampleRate / cfg.blockSize);
    double best = 0.0;

    // Best of two passes: the realtime figure is otherwise sensitive to
    // background load on the machine running the tests.
    for (int pass = 0; pass < 2; ++pass)
    {
        const double start = juce::Time::getMillisecondCounterHiRes();

        for (int block = 0; block < blocks; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                buffer.copyFrom (channel, 0, source, channel, 0, cfg.blockSize);

            processor.processBlock (buffer, midi);
        }

        const double elapsed = juce::Time::getMillisecondCounterHiRes() - start;

        if (elapsed > 0.0)
            best = std::max (best, (seconds * 1000.0) / elapsed);
    }

    return best;
}

void resetToBaseline (PolarityAudioProcessor& processor, AudioConfig cfg = {})
{
    processor.setActiveSlot (0);
    processor.setQuality (1);
    processor.setCurveSmoothing (2);

    auto& apvts = processor.getValueTreeState();
    const auto setValue = [&apvts] (const char* id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (value);
    };

    setValue ("mix", 1.0f);
    setValue ("bypass", 0.0f);
    setValue ("deltaOut", 0.0f);

    processor.prepareToPlay (cfg.sampleRate, cfg.blockSize);
}

bool testSampleRateMatrix (PolarityAudioProcessor& processor)
{
    struct RateRow
    {
        double rate;
        double nullThreshold;
        double deltaThreshold;
    };

    const RateRow rows[] = {
        { 44100.0, -50.0, -100.0 },
        { 48000.0, -50.0, -100.0 },
        { 96000.0, -40.0, -100.0 },
        { 192000.0, -30.0, -90.0 }
    };

    bool ok = true;

    const auto resetRouting = [&processor]
    {
        auto& apvts = processor.getValueTreeState();

        if (auto* p = apvts.getParameter ("mix"))
            p->setValueNotifyingHost (1.0f);

        if (auto* p = apvts.getParameter ("bypass"))
            p->setValueNotifyingHost (0.0f);

        if (auto* p = apvts.getParameter ("deltaOut"))
            p->setValueNotifyingHost (0.0f);
    };

    for (const auto& row : rows)
    {
        const AudioConfig cfg { row.rate, blockSize };

        resetRouting();
        processor.setQuality (1);
        processor.setCurveSmoothing (2);
        processor.setActiveSlot (0);
        processor.setCurves (wideNullCurve(), flatCurve());

        const double nullDb = measureSineGainDb (processor, 1000.0, 0, cfg, 0.5);
        const double passDb = measureSineGainDb (processor, 10000.0, 1, cfg, 0.5);
        const int latency = processor.getLatencySamples();

        if (auto* delta = processor.getValueTreeState().getParameter ("deltaOut"))
            delta->setValueNotifyingHost (1.0f);

        const double deltaDb = measureSineGainDb (processor, 1000.0, 1, cfg, 0.5);

        if (auto* delta = processor.getValueTreeState().getParameter ("deltaOut"))
            delta->setValueNotifyingHost (0.0f);

        const double dryError = measureDelayedDryError (processor, 1.0f, true, cfg, 0.5);

        std::printf ("rate matrix %6.0f Hz : null %6.1f dB  pass %+5.2f dB  "
                     "delta-flat %6.1f dB  dry %.1e  lat %d\n",
                     row.rate, nullDb, passDb, deltaDb, dryError, latency);

        ok = ok && latency == 2048
                 && nullDb < row.nullThreshold
                 && std::abs (passDb) < 0.1
                 && deltaDb < row.deltaThreshold
                 && dryError < 1.0e-6;
    }

    const int blockRows[] = { 32, 512, 2048 };

    for (const int bs : blockRows)
    {
        const AudioConfig cfg { kSampleRate, bs };

        resetRouting();
        processor.setQuality (1);
        processor.setCurveSmoothing (2);
        processor.setActiveSlot (0);
        processor.setCurves (lowNullCurve(), flatCurve());

        const double nullDb = measureSineGainDb (processor, 400.0, 0, cfg, 0.5);
        const double passDb = measureSineGainDb (processor, 5000.0, 1, cfg, 0.5);
        const double dryError = measureDelayedDryError (processor, 1.0f, true, cfg, 0.5);

        std::printf ("block matrix %4d    : null %6.1f dB  pass %+5.2f dB  dry %.1e\n",
                     bs, nullDb, passDb, dryError);

        ok = ok && nullDb < -40.0
                 && std::abs (passDb) < 0.1
                 && dryError < 1.0e-6;
    }

    resetToBaseline (processor);
    return ok;
}

bool testBusesLayout (PolarityAudioProcessor& processor)
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto disabled = juce::AudioChannelSet::disabled();
    const auto surround = juce::AudioChannelSet::create5point1();

    const auto makeLayout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        return layout;
    };

    const bool layoutsOk =
        processor.isBusesLayoutSupported (makeLayout (mono, mono))
        && processor.isBusesLayoutSupported (makeLayout (stereo, stereo))
        && ! processor.isBusesLayoutSupported (makeLayout (mono, stereo))
        && ! processor.isBusesLayoutSupported (makeLayout (stereo, mono))
        && ! processor.isBusesLayoutSupported (makeLayout (disabled, disabled))
        && ! processor.isBusesLayoutSupported (makeLayout (surround, surround));

    PolarityAudioProcessor monoProcessor;
    monoProcessor.setPlayConfigDetails (1, 1, static_cast<int> (kSampleRate), blockSize);
    monoProcessor.setCurvePoints (0, lowNullCurve());
    monoProcessor.setCurvePoints (1, flatCurve());

    const double monoNull = measureSineGainDb (monoProcessor, 400.0, 0);
    const double monoPass = measureSineGainDb (monoProcessor, 5000.0, 0);
    const float sideDb = monoProcessor.getMeterSideDb();
    const float correlation = monoProcessor.getMeterCorrelation();

    const bool monoOk = monoNull < -40.0
        && std::abs (monoPass) < 0.1
        && sideDb < -60.0f
        && correlation > 0.95f;

    // Mono must produce the same left channel as stereo with the same L curve.
    PolarityAudioProcessor stereoProcessor;
    stereoProcessor.setPlayConfigDetails (2, 2, static_cast<int> (kSampleRate), blockSize);
    stereoProcessor.setCurvePoints (0, lowNullCurve());
    stereoProcessor.setCurvePoints (1, flatCurve());

    monoProcessor.prepareToPlay (kSampleRate, blockSize);
    stereoProcessor.prepareToPlay (kSampleRate, blockSize);

    juce::AudioBuffer<float> monoBuffer (1, blockSize);
    juce::AudioBuffer<float> stereoBuffer (2, blockSize);
    juce::MidiBuffer midi;
    double maxDifference = 0.0;
    const int total = static_cast<int> (kSampleRate * 0.2);

    for (int position = 0; position < total; position += blockSize)
    {
        const int n = std::min (blockSize, total - position);
        fillSine (monoBuffer, position, n, 1000.0, kSampleRate);
        fillSine (stereoBuffer, position, n, 1000.0, kSampleRate);

        juce::AudioBuffer<float> monoView (monoBuffer.getArrayOfWritePointers(), 1, n);
        juce::AudioBuffer<float> stereoView (stereoBuffer.getArrayOfWritePointers(), 2, n);

        monoProcessor.processBlock (monoView, midi);
        stereoProcessor.processBlock (stereoView, midi);

        for (int i = 0; i < n; ++i)
            maxDifference = std::max (
                maxDifference,
                std::abs (static_cast<double> (monoView.getSample (0, i))
                          - static_cast<double> (stereoView.getSample (0, i))));
    }

    std::printf ("buses            : layouts %s  mono null %.1f dB pass %+5.2f dB  "
                 "side %.0f  corr %.2f  parity %.1e\n",
                 layoutsOk ? "PASS" : "FAIL", monoNull, monoPass,
                 sideDb, correlation, maxDifference);

    return layoutsOk && monoOk && maxDifference < 1.0e-6;
}

bool testLiveSettingsSwitch (PolarityAudioProcessor& processor)
{
    processor.setQuality (1);
    processor.setCurveSmoothing (2);
    processor.setActiveSlot (0);
    processor.setCurves (flatCurve(), flatCurve());
    processor.setActiveSlot (1);
    processor.setCurves (lowNullCurve(), flatCurve());
    processor.setActiveSlot (0);

    auto& apvts = processor.getValueTreeState();
    const auto setValue = [&apvts] (const char* id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (value);
    };

    setValue ("mix", 1.0f);
    setValue ("bypass", 0.0f);
    setValue ("deltaOut", 0.0f);
    processor.prepareToPlay (kSampleRate, blockSize);

    const auto setChoice = [&] (const char* id, int index)
    {
        if (auto* parameter = apvts.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (static_cast<float> (index)));
            parameter->endChangeGesture();
        }

        processor.flushPendingParameterChanges();
    };

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const int total = static_cast<int> (kSampleRate * 3.0);

    double previous = 0.0;
    double maxStep = 0.0;
    double peak = 0.0;
    double tailSum = 0.0;
    int tailCount = 0;
    bool finite = true;
    bool qualityHigh = false;
    bool edgeOff = false;
    bool slotB = false;
    bool restored = false;

    for (int position = 0; position < total; position += blockSize)
    {
        const int n = std::min (blockSize, total - position);
        fillSine (buffer, position, n, 1000.0, kSampleRate);
        const double time = static_cast<double> (position) / kSampleRate;

        if (! qualityHigh && time >= 0.5)
        {
            setChoice ("quality", 2);
            qualityHigh = processor.getLatencySamples() == 4096;
        }

        if (! edgeOff && time >= 1.0)
        {
            setChoice ("edge", 0);
            edgeOff = processor.getCurveSmoothing() == 0;
        }

        if (! slotB && time >= 1.5)
        {
            setChoice ("slot", 1);
            slotB = processor.getActiveSlot() == 1;
        }

        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        processor.processBlock (view, midi);

        for (int i = 0; i < n; ++i)
        {
            const double value = view.getSample (0, i);
            const double absolute = std::abs (value);

            if (! std::isfinite (value))
                finite = false;

            peak = std::max (peak, absolute);

            if (position + i > 0)
                maxStep = std::max (maxStep, std::abs (value - previous));

            previous = value;

            const double sampleTime = static_cast<double> (position + i) / kSampleRate;

            if (sampleTime >= 2.0 && sampleTime < 2.5)
            {
                tailSum += value * value;
                ++tailCount;
            }
        }

        if (! restored && time >= 2.5)
        {
            setChoice ("slot", 0);
            setChoice ("quality", 1);
            restored = processor.getLatencySamples() == 2048
                    && processor.getActiveSlot() == 0;
        }
    }

    const double tailDb = 20.0 * std::log10 (
        std::max (std::sqrt (tailSum / std::max (1, tailCount))
                      * std::sqrt (2.0) / 0.5,
                  1.0e-15));

    std::printf ("live switch      : finite %d  peak %.2f  maxstep %.3f  "
                 "null tail %.1f dB  q %d edge %d slot %d restore %d\n",
                 finite ? 1 : 0, peak, maxStep, tailDb,
                 qualityHigh ? 1 : 0, edgeOff ? 1 : 0, slotB ? 1 : 0,
                 restored ? 1 : 0);

    const bool ok = finite
        && peak <= 0.75
        && maxStep < 0.2
        && tailDb < -40.0
        && qualityHigh
        && edgeOff
        && slotB
        && restored;

    resetToBaseline (processor);
    return ok;
}

bool testHostParameters (PolarityAudioProcessor& processor)
{
    auto& apvts = processor.getValueTreeState();

    auto* mix = apvts.getParameter ("mix");
    auto* delta = apvts.getParameter ("deltaOut");
    auto* slot = apvts.getParameter ("slot");
    auto* quality = apvts.getParameter ("quality");
    auto* edge = apvts.getParameter ("edge");

    if (mix == nullptr || delta == nullptr || slot == nullptr
        || quality == nullptr || edge == nullptr)
        return false;

    // Call getText through the base interface: the derived overrides are private.
    const bool metadataOk = mix->getName (128) == "Mix"
        && mix->getText (mix->convertTo0to1 (0.5f), 128) == "50%"
        && delta->getText (1.0f, 128) == "DELTA"
        && delta->getText (0.0f, 128) == "NORM"
        && quality->getText (quality->convertTo0to1 (2.0f), 128) == "8192 taps"
        && ! quality->isAutomatable()
        && slot->isAutomatable()
        && edge->isAutomatable();

    const auto setChoice = [] (juce::RangedAudioParameter& parameter, int index)
    {
        parameter.beginChangeGesture();
        parameter.setValueNotifyingHost (
            parameter.convertTo0to1 (static_cast<float> (index)));
        parameter.endChangeGesture();
    };

    setChoice (*slot, 1);
    processor.flushPendingParameterChanges();
    const bool slotApplied = processor.getActiveSlot() == 1;

    setChoice (*quality, 2);
    processor.flushPendingParameterChanges();
    const bool qualityApplied = processor.getQuality() == 2
                                && processor.getLatencySamples() == 4096;

    setChoice (*edge, 0);
    processor.flushPendingParameterChanges();
    const bool edgeApplied = processor.getCurveSmoothing() == 0;

    // State round-trip with the new parameters.
    processor.setActiveSlot (1);
    processor.setQuality (2);
    processor.setCurveSmoothing (4);
    processor.prepareToPlay (kSampleRate, blockSize);

    juce::MemoryBlock saved;
    processor.getStateInformation (saved);

    bool versionOk = false;

    if (auto xml = juce::AudioProcessor::getXmlFromBinary (
            saved.getData(), static_cast<int> (saved.getSize())))
    {
        versionOk = xml->getIntAttribute ("stateVersion", -1)
                    == PolarityAudioProcessor::getStateVersion();
    }

    PolarityAudioProcessor restored;
    restored.setPlayConfigDetails (2, 2, static_cast<int> (kSampleRate), blockSize);
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    restored.prepareToPlay (kSampleRate, blockSize);

    const bool stateOk = restored.getActiveSlot() == 1
        && restored.getQuality() == 2
        && restored.getCurveSmoothing() == 4
        && restored.getLatencySamples() == 4096;

    // A versioned state's PARAM children win over a stale root "quality".
    bool conflictOk = false;

    if (auto xml = juce::AudioProcessor::getXmlFromBinary (
            saved.getData(), static_cast<int> (saved.getSize())))
    {
        xml->setAttribute ("quality", 0);

        juce::MemoryBlock conflicting;
        juce::AudioProcessor::copyXmlToBinary (*xml, conflicting);

        PolarityAudioProcessor conflict;
        conflict.setPlayConfigDetails (2, 2, static_cast<int> (kSampleRate), blockSize);
        conflict.setStateInformation (conflicting.getData(),
                                      static_cast<int> (conflicting.getSize()));
        conflict.prepareToPlay (kSampleRate, blockSize);

        conflictOk = conflict.getQuality() == 2
                  && conflict.getLatencySamples() == 4096;
    }

    // A future schema version must load best-effort and re-save as version 1.
    bool forwardOk = false;

    if (auto xml = juce::AudioProcessor::getXmlFromBinary (
            saved.getData(), static_cast<int> (saved.getSize())))
    {
        xml->setAttribute ("stateVersion", 99);

        juce::MemoryBlock forward;
        juce::AudioProcessor::copyXmlToBinary (*xml, forward);

        PolarityAudioProcessor migrated;
        migrated.setPlayConfigDetails (2, 2, static_cast<int> (kSampleRate), blockSize);
        migrated.setStateInformation (forward.getData(),
                                      static_cast<int> (forward.getSize()));
        migrated.prepareToPlay (kSampleRate, blockSize);

        juce::MemoryBlock resaved;
        migrated.getStateInformation (resaved);

        if (auto resavedXml = juce::AudioProcessor::getXmlFromBinary (
                resaved.getData(), static_cast<int> (resaved.getSize())))
        {
            forwardOk = migrated.getActiveSlot() == 1
                     && migrated.getQuality() == 2
                     && resavedXml->getIntAttribute ("stateVersion", -1)
                            == PolarityAudioProcessor::getStateVersion();
        }
    }

    // Legacy state: no slot/quality/edge PARAM children, no schema version,
    // old root quality property, and the slot stored only in CURVES.
    bool legacyOk = false;

    if (auto xml = juce::AudioProcessor::getXmlFromBinary (
            saved.getData(), static_cast<int> (saved.getSize())))
    {
        for (int i = xml->getNumChildElements(); --i >= 0;)
        {
            auto* child = xml->getChildElement (i);
            const auto id = child->getStringAttribute ("id");

            if (id == "slot" || id == "quality" || id == "edge")
                xml->removeChildElement (child, true);
        }

        xml->removeAttribute ("stateVersion");
        xml->setAttribute ("quality", 2);

        if (auto* curves = xml->getChildByName ("CURVES"))
            curves->setAttribute ("active", 1);

        juce::MemoryBlock legacy;
        juce::AudioProcessor::copyXmlToBinary (*xml, legacy);

        PolarityAudioProcessor migrated;
        migrated.setPlayConfigDetails (2, 2, static_cast<int> (kSampleRate), blockSize);
        migrated.setStateInformation (legacy.getData(),
                                      static_cast<int> (legacy.getSize()));
        migrated.prepareToPlay (kSampleRate, blockSize);

        legacyOk = migrated.getActiveSlot() == 1
            && migrated.getQuality() == 2
            && migrated.getCurveSmoothing() == 2
            && migrated.getLatencySamples() == 4096;
    }

    std::printf ("host params      : meta %s slot %s quality %s edge %s state %s "
                 "version %s conflict %s forward %s legacy %s\n",
                 metadataOk ? "PASS" : "FAIL",
                 slotApplied ? "PASS" : "FAIL",
                 qualityApplied ? "PASS" : "FAIL",
                 edgeApplied ? "PASS" : "FAIL",
                 stateOk ? "PASS" : "FAIL",
                 versionOk ? "PASS" : "FAIL",
                 conflictOk ? "PASS" : "FAIL",
                 forwardOk ? "PASS" : "FAIL",
                 legacyOk ? "PASS" : "FAIL");

    processor.setActiveSlot (0);
    processor.setQuality (1);
    processor.setCurveSmoothing (2);
    processor.prepareToPlay (kSampleRate, blockSize);

    return metadataOk && slotApplied && qualityApplied && edgeApplied
        && stateOk && versionOk && conflictOk && forwardOk && legacyOk;
}
} // namespace

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;

    PolarityAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, static_cast<int> (kSampleRate), blockSize);

    processor.setCurvePoints (0, lowNullCurve());
    processor.setCurvePoints (1, flatCurve());

    processor.prepareToPlay (kSampleRate, blockSize);

    const int latency = processor.getLatencySamples();
    const double leftNull = measureSineGainDb (processor, 400.0, 0);
    const double rightPass = measureSineGainDb (processor, 400.0, 1);
    const double leftHighPass = measureSineGainDb (processor, 5000.0, 0);
    const double mix0Error = measureDelayedDryError (processor, 0.0f, false);
    const double bypassError = measureDelayedDryError (processor, 1.0f, true);

    double crossfadeTailDb = 0.0;
    const double crossfadeStep = measureCrossfadeClick (processor, crossfadeTailDb);

    const bool abOk = testABSlots (processor);
    const bool presetOk = testPresets (processor);

    const auto correlatedMeters = measureMeters (processor, false);
    const auto antiPhaseMeters = measureMeters (processor, true);
    const bool metersOk = correlatedMeters.correlation > 0.95f
                       && correlatedMeters.sideDb < -60.0f
                       && antiPhaseMeters.correlation < -0.95f
                       && antiPhaseMeters.midDb < -60.0f;

    const auto analysis = testAnalysis (processor);
    const bool analysisOk = analysis.ok;

    const bool deltaOk = testDeltaMode (processor);
    const bool qualityOk = testQuality (processor);
    const bool hostParametersOk = testHostParameters (processor);
    const bool rateMatrixOk = testSampleRateMatrix (processor);
    const bool busesOk = testBusesLayout (processor);
    const bool liveSwitchOk = testLiveSettingsSwitch (processor);

    const double benchLow = benchmarkProcessing (processor, 1.0, 0);
    const double benchNormal = benchmarkProcessing (processor, 1.0, 1);
    const double benchHigh = benchmarkProcessing (processor, 1.0, 2);
    processor.setQuality (1);
    processor.prepareToPlay (kSampleRate, blockSize);

    processor.setCurvePoints (0, lowNullCurve());
    processor.setCurvePoints (1, highNullCurve());

    bool editorOk = false;
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editorOk = editor != nullptr;
    }

    std::printf ("latency          : %d samples\n", latency);
    std::printf ("L 400 Hz null    : %.1f dB\n", leftNull);
    std::printf ("R 400 Hz pass    : %.1f dB\n", rightPass);
    std::printf ("L 5 kHz pass     : %.1f dB\n", leftHighPass);
    std::printf ("mix=0 dry error  : %.3e\n", mix0Error);
    std::printf ("bypass error     : %.3e\n", bypassError);
    std::printf ("crossfade maxstep: %.4f\n", crossfadeStep);
    std::printf ("crossfade tail   : %.1f dB\n", crossfadeTailDb);
    std::printf ("A/B slots        : %s\n", abOk ? "PASS" : "FAIL");
    std::printf ("preset roundtrip : %s\n", presetOk ? "PASS" : "FAIL");
    std::printf ("meters corr/anti : %+.2f / %+.2f  (side %.1f / mid %.1f dB)\n",
                 correlatedMeters.correlation,
                 antiPhaseMeters.correlation,
                 correlatedMeters.sideDb,
                 antiPhaseMeters.midDb);
    std::printf ("meters test      : %s\n", metersOk ? "PASS" : "FAIL");
    std::printf ("analysis f/n     : %.1f/%.1f -> %.1f/%.1f dB  wave %.4f\n",
                 analysis.inFlat, analysis.outFlat,
                 analysis.inNull, analysis.outNull,
                 analysis.maxWave);
    std::printf ("analysis test    : %s\n", analysisOk ? "PASS" : "FAIL");
    std::printf ("quality test     : %s\n", qualityOk ? "PASS" : "FAIL");
    std::printf ("host params test : %s\n", hostParametersOk ? "PASS" : "FAIL");
    std::printf ("rate matrix      : %s\n", rateMatrixOk ? "PASS" : "FAIL");
    std::printf ("buses test       : %s\n", busesOk ? "PASS" : "FAIL");
    std::printf ("live switch test : %s\n", liveSwitchOk ? "PASS" : "FAIL");
    std::printf ("benchmark        : %.1fx / %.1fx / %.1fx realtime (2048/4096/8192 taps)\n",
                 benchLow, benchNormal, benchHigh);
    std::printf ("editor smoke     : %s\n", editorOk ? "PASS" : "FAIL");

    const bool ok = latency == 2048
                 && leftNull < -40.0
                 && std::abs (rightPass) < 0.1
                 && std::abs (leftHighPass) < 0.1
                 && mix0Error < 1.0e-6
                 && bypassError < 1.0e-6
                 && crossfadeStep < 0.15
                 && crossfadeTailDb < -40.0
                 && abOk
                 && presetOk
                 && metersOk
                 && analysisOk
                 && deltaOk
                 && qualityOk
                 && hostParametersOk
                 && rateMatrixOk
                 && busesOk
                 && liveSwitchOk
                 && editorOk;

    std::printf ("PROCESSOR TEST   : %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
