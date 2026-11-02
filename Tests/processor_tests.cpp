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

void fillSine (juce::AudioBuffer<float>& buffer, int startSample, double freq)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (channel, i,
                              0.5f * static_cast<float> (std::sin (
                                  2.0 * kPi * freq
                                  * static_cast<double> (startSample + i) / kSampleRate)));
}

double measureSineGainDb (PolarityAudioProcessor& processor,
                          double freq,
                          int channel)
{
    processor.prepareToPlay (kSampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double sum = 0.0;
    int count = 0;

    for (int position = 0; position < static_cast<int> (kSampleRate);
         position += blockSize)
    {
        fillSine (buffer, position, freq);
        processor.processBlock (buffer, midi);

        if (position >= static_cast<int> (kSampleRate * 0.5))
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double value = buffer.getSample (channel, i);
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
                               bool bypassed)
{
    auto* mixParameter = processor.getValueTreeState().getParameter ("mix");
    auto* bypassParameter = processor.getValueTreeState().getParameter ("bypass");

    if (mixParameter == nullptr || bypassParameter == nullptr)
        return 1.0e9;

    mixParameter->setValueNotifyingHost (mix);
    bypassParameter->setValueNotifyingHost (bypassed ? 1.0f : 0.0f);
    processor.prepareToPlay (kSampleRate, blockSize);

    const int total = static_cast<int> (kSampleRate);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    std::mt19937 rng (3);
    std::normal_distribution<float> noise (0.0f, 0.1f);

    std::vector<float> input (static_cast<std::size_t> (total));
    std::vector<float> output (static_cast<std::size_t> (total));

    for (auto& value : input)
        value = noise (rng);

    for (int position = 0; position < total; position += blockSize)
    {
        const int count = std::min (blockSize, total - position);

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i)
                buffer.setSample (channel, i,
                                  input[static_cast<std::size_t> (position + i)]);

        processor.processBlock (buffer, midi);

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i)
                output[static_cast<std::size_t> (position + i)]
                    = buffer.getSample (channel, i);
    }

    const int latency = processor.getLatencySamples();
    double error = 0.0;

    for (int i = 0; i < total - latency; ++i)
        error = std::max (error,
                          std::abs (static_cast<double> (output[static_cast<std::size_t> (i + latency)])
                                    - static_cast<double> (input[static_cast<std::size_t> (i)])));

    return error;
}

double measureCrossfadeClick (PolarityAudioProcessor& processor, double& tailDb)
{
    if (auto* mix = processor.getValueTreeState().getParameter ("mix"))
        mix->setValueNotifyingHost (1.0f);

    if (auto* bypass = processor.getValueTreeState().getParameter ("bypass"))
        bypass->setValueNotifyingHost (0.0f);

    processor.setCurvePoints (0, flatCurve());
    processor.setCurvePoints (1, flatCurve());
    processor.prepareToPlay (kSampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double previous = 0.0;
    double maxStep = 0.0;
    double tailSum = 0.0;
    int tailCount = 0;
    bool changed = false;

    for (int position = 0; position < static_cast<int> (kSampleRate); position += blockSize)
    {
        fillSine (buffer, position, 1000.0);

        if (! changed && position >= static_cast<int> (kSampleRate * 0.25))
        {
            processor.setCurvePoints (0, lowNullCurve());
            processor.setCurvePoints (1, lowNullCurve());
            changed = true;
        }

        processor.processBlock (buffer, midi);

        for (int i = 0; i < blockSize; ++i)
        {
            const double value = buffer.getSample (0, i);

            if (position + i > 0)
                maxStep = std::max (maxStep, std::abs (value - previous));

            previous = value;

            if (position + i >= static_cast<int> (kSampleRate * 0.6))
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

AnalysisResult testAnalysis (PolarityAudioProcessor& processor)
{
    if (auto* mix = processor.getValueTreeState().getParameter ("mix"))
        mix->setValueNotifyingHost (1.0f);

    if (auto* bypass = processor.getValueTreeState().getParameter ("bypass"))
        bypass->setValueNotifyingHost (0.0f);

    processor.setActiveSlot (0);
    processor.setCurves (flatCurve(), flatCurve());
    processor.prepareToPlay (kSampleRate, blockSize);
    processor.setWaveformWindowMs (170.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    const auto runSine = [&] (double seconds)
    {
        const int total = static_cast<int> (kSampleRate * seconds);

        for (int position = 0; position < total; position += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float value = 0.5f * static_cast<float> (
                    std::sin (2.0 * kPi * 1000.0
                              * static_cast<double> (position + i) / kSampleRate));

                buffer.setSample (0, i, value);
                buffer.setSample (1, i, value);
            }

            processor.processBlock (buffer, midi);
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
} // namespace

int main()
{
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
                 && editorOk;

    std::printf ("PROCESSOR TEST   : %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
