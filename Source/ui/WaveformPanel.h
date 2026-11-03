#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

class PolarityAudioProcessor;

/** Input/output waveform comparison with selectable RMS / Peak metric. */
class WaveformPanel final : public juce::Component,
                            public juce::SettableTooltipClient,
                            private juce::Timer
{
public:
    enum class Metric { Rms, Peak };

    explicit WaveformPanel (PolarityAudioProcessor&);
    ~WaveformPanel() override = default;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void setFrozen (bool shouldFreeze);
    void setMetric (Metric newMetric);
    Metric getMetric() const noexcept { return metric; }

    /** Header strip in local coordinates, shared with the editor so the
        WAVE / METRIC controls can sit inside it. */
    juce::Rectangle<int> getHeaderBounds() const;

    /** Space reserved on the header's right for the painted IN/OUT legend. */
    static constexpr int headerLegendWidth = 54;

private:
    void timerCallback() override;

    float inputRmsAt (int point) const;
    float outputRmsAt (int point) const;
    float inputPeakAt (int point) const;
    float outputPeakAt (int point) const;

    PolarityAudioProcessor& processorRef;

    std::vector<float> frozenInputRms;
    std::vector<float> frozenOutputRms;
    std::vector<float> frozenInputMin;
    std::vector<float> frozenInputMax;
    std::vector<float> frozenOutputMin;
    std::vector<float> frozenOutputMax;

    int frozenWindowSamples = 0;
    bool frozen = false;
    Metric metric = Metric::Rms;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformPanel)
};
