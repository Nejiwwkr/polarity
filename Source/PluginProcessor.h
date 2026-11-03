#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/CurveModel.h"
#include "dsp/InversionEngine.h"

#include <array>
#include <atomic>
#include <vector>

class PolarityAudioProcessor final
    : public juce::AudioProcessor,
      private juce::AudioProcessorValueTreeState::Listener,
      private juce::AsyncUpdater
{
public:
    PolarityAudioProcessor();
    ~PolarityAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    juce::AudioProcessorParameter* getBypassParameter() const override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    int getEngineLatencySamples() const noexcept { return engine.getLatencySamples(); }

    /** FIR quality: 0 = 2048 taps, 1 = 4096 taps, 2 = 8192 taps. */
    static constexpr int getQualityChoiceCount() noexcept { return 3; }

    static constexpr int getQualityTaps (int choice) noexcept
    {
        return choice <= 0 ? 2048 : (choice == 1 ? 4096 : 8192);
    }

    int getQuality() const noexcept
    {
        return qualityChoice.load (std::memory_order_relaxed);
    }

    /** Changing quality changes the reported latency; the host re-syncs PDC. */
    void setQuality (int choice);

    /** Applies queued host parameter changes synchronously (message thread). */
    void flushPendingParameterChanges();

    static constexpr int getEdgeChoiceCount() noexcept { return 4; }

    static int getEdgeBinsForChoice (int choice) noexcept
    {
        constexpr int bins[] = { 0, 1, 2, 4 };
        return bins[choice <= 0 ? 0 : (choice >= 3 ? 3 : choice)];
    }

    static constexpr int getEdgeChoiceForBins (int bins) noexcept
    {
        return bins <= 0 ? 0 : (bins <= 1 ? 1 : (bins <= 2 ? 2 : 3));
    }

    /** channel: 0 = left, 1 = right. */
    std::vector<polarity::dsp::CurvePoint> getCurvePoints (int channel) const;
    void setCurvePoints (int channel, std::vector<polarity::dsp::CurvePoint> points);
    void setCurves (std::vector<polarity::dsp::CurvePoint> leftPoints,
                    std::vector<polarity::dsp::CurvePoint> rightPoints);

    /** A/B curve slots.  slot is 0 (A) or 1 (B). */
    int getActiveSlot() const noexcept { return activeSlot.load(); }
    void setActiveSlot (int slot);

    /** Broadcast when curves are replaced from host state or the A/B slot changes. */
    juce::ChangeBroadcaster& getCurveBroadcaster() noexcept { return curveBroadcaster; }

    float getMeterMidDb() const noexcept { return meterMidDb.load (std::memory_order_relaxed); }
    float getMeterSideDb() const noexcept { return meterSideDb.load (std::memory_order_relaxed); }
    float getMeterCorrelation() const noexcept
    {
        return meterCorrelation.load (std::memory_order_relaxed);
    }

    static constexpr int getSpectrumBandCount() noexcept { return spectrumBands; }
    static constexpr int getWaveformPointCount() noexcept { return waveformPoints; }

    float getSpectrumInput (int band) const noexcept
    {
        return spectrumInput[static_cast<std::size_t> (
                    juce::jlimit (0, spectrumBands - 1, band))]
            .load (std::memory_order_relaxed);
    }

    float getSpectrumOutput (int band) const noexcept
    {
        return spectrumOutput[static_cast<std::size_t> (
                    juce::jlimit (0, spectrumBands - 1, band))]
            .load (std::memory_order_relaxed);
    }

    float getWaveformInputMin (int point) const noexcept
    {
        return waveformInputMin[static_cast<std::size_t> (
                    juce::jlimit (0, waveformPoints - 1, point))]
            .load (std::memory_order_relaxed);
    }

    float getWaveformInputMax (int point) const noexcept
    {
        return waveformInputMax[static_cast<std::size_t> (
                    juce::jlimit (0, waveformPoints - 1, point))]
            .load (std::memory_order_relaxed);
    }

    float getWaveformOutputMin (int point) const noexcept
    {
        return waveformOutputMin[static_cast<std::size_t> (
                    juce::jlimit (0, waveformPoints - 1, point))]
            .load (std::memory_order_relaxed);
    }

    float getWaveformOutputMax (int point) const noexcept
    {
        return waveformOutputMax[static_cast<std::size_t> (
                    juce::jlimit (0, waveformPoints - 1, point))]
            .load (std::memory_order_relaxed);
    }

    float getWaveformInputRms (int point) const noexcept
    {
        return waveformInputRms[static_cast<std::size_t> (
                    juce::jlimit (0, waveformPoints - 1, point))]
            .load (std::memory_order_relaxed);
    }

    float getWaveformOutputRms (int point) const noexcept
    {
        return waveformOutputRms[static_cast<std::size_t> (
                    juce::jlimit (0, waveformPoints - 1, point))]
            .load (std::memory_order_relaxed);
    }

    /** Waveform time window in milliseconds; 200 .. 5000. */
    void setWaveformWindowMs (float milliseconds) noexcept
    {
        waveformWindowMs.store (juce::jlimit (200.0f, 5000.0f, milliseconds),
                                std::memory_order_relaxed);
    }

    float getWaveformWindowMs() const noexcept
    {
        return waveformWindowMs.load (std::memory_order_relaxed);
    }

    int getWaveformWindowSamples() const noexcept
    {
        const double sampleRate = currentSampleRate.load (std::memory_order_relaxed);

        if (sampleRate <= 0.0 || maxWaveformSamples <= 0)
            return 0;

        const int samples = juce::roundToInt (
            waveformWindowMs.load (std::memory_order_relaxed) * sampleRate / 1000.0);

        return juce::jlimit (1024, maxWaveformSamples, samples);
    }

    /** Spectrum smoothing radius in log bands; 0 = off, 2 ~ 1/12 oct, 4 ~ 1/6, 8 ~ 1/3. */
    void setSpectrumSmoothing (int radiusBands) noexcept
    {
        spectrumSmoothingRadius.store (
            juce::jlimit (0, 16, radiusBands),
            std::memory_order_relaxed);
    }

    int getSpectrumSmoothing() const noexcept
    {
        return spectrumSmoothingRadius.load (std::memory_order_relaxed);
    }

    /** Spectrum temporal smoothing time constant in milliseconds. */
    void setSpectrumTimeSmoothing (float milliseconds) noexcept
    {
        spectrumTimeSmoothingMs.store (
            juce::jlimit (10.0f, 2000.0f, milliseconds),
            std::memory_order_relaxed);
    }

    float getSpectrumTimeSmoothing() const noexcept
    {
        return spectrumTimeSmoothingMs.load (std::memory_order_relaxed);
    }

    /** FIR edge smoothing radius in FFT bins; 0 = exact curve, 2 = gentler edges. */
    void setCurveSmoothing (int bins);
    int getCurveSmoothing() const noexcept
    {
        return curveSmoothingBins.load (std::memory_order_relaxed);
    }

private:
    struct CurveSlot
    {
        polarity::dsp::CurveModel left;
        polarity::dsp::CurveModel right;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static std::vector<polarity::dsp::CurvePoint> defaultCurvePoints();
    static juce::ValueTree curveToTree (const juce::String& channel,
                                        const polarity::dsp::CurveModel& curve);
    static void readCurve (const juce::ValueTree& parent,
                           const juce::String& channel,
                           polarity::dsp::CurveModel& model);

    void writeCurvesToState();
    void loadCurvesFromState();
    void rebuildKernels();
    void updateAnalysis (int wetLatency, int blockSamples);
    void resetAnalysis();

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void applyPendingSettings();
    void syncPendingFromParameters();
    int parameterIndex (const juce::String& id, int fallback) const;
    void writeParameterValue (const juce::String& id, int value);
    void scheduleSettingApply();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* deltaOutParam = nullptr;

    mutable juce::CriticalSection curveLock;
    std::array<CurveSlot, 2> slots;
    std::atomic<int> activeSlot { 0 };
    juce::ChangeBroadcaster curveBroadcaster;
    std::atomic<double> currentSampleRate { 0.0 };

    std::atomic<float> meterMidDb { -72.0f };
    std::atomic<float> meterSideDb { -72.0f };
    std::atomic<float> meterCorrelation { 0.0f };

    static constexpr int fftOrder = 14;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int spectrumBands = 512;
    static constexpr int waveformPoints = 512;
    int maxWaveformSamples = 1;

    juce::dsp::FFT analysisFft { fftOrder };
    std::vector<float> analysisWork;
    std::vector<float> hannWindow;
    std::vector<float> inputAnalysisRing;
    std::vector<float> outputAnalysisRing;
    int inputAnalysisWrite = 0;
    int outputAnalysisWrite = 0;
    std::atomic<float> waveformWindowMs { 1000.0f };
    int waveformUpdateCounter = 0;
    std::atomic<int> spectrumSmoothingRadius { 2 };
    std::atomic<float> spectrumTimeSmoothingMs { 200.0f };
    std::atomic<int> curveSmoothingBins { 2 };
    std::array<std::atomic<float>, spectrumBands> spectrumInput;
    std::array<std::atomic<float>, spectrumBands> spectrumOutput;
    std::array<std::atomic<float>, waveformPoints> waveformInputMin;
    std::array<std::atomic<float>, waveformPoints> waveformInputMax;
    std::array<std::atomic<float>, waveformPoints> waveformOutputMin;
    std::array<std::atomic<float>, waveformPoints> waveformOutputMax;
    std::array<std::atomic<float>, waveformPoints> waveformInputRms;
    std::array<std::atomic<float>, waveformPoints> waveformOutputRms;

    polarity::dsp::InversionEngine engine;
    juce::AudioBuffer<float> dryCopy;
    std::vector<std::vector<float>> dryDelay;
    std::vector<int> dryDelayIndex;

    std::array<float, 2> deltaAmounts { 0.0f, 0.0f };
    float deltaSmoothingCoeff = 1.0f;

    /** Everything is prepared for the largest kernel so quality switches are
        allocation-free and only the active kernel length changes. */
    static constexpr int maxTaps = 8192;
    std::atomic<int> qualityChoice { 1 };
    std::atomic<int> activeLatency { 2048 };

    std::atomic<int> pendingSlot { 0 };
    std::atomic<int> pendingQuality { 1 };
    std::atomic<int> pendingEdgeChoice { 2 };
    std::atomic<bool> loadingState { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolarityAudioProcessor)
};
