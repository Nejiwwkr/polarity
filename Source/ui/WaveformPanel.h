#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

class PolarityAudioProcessor;

/** Input/output waveform comparison with a filled area and adjustable window. */
class WaveformPanel final : public juce::Component,
                            private juce::Timer
{
public:
    explicit WaveformPanel (PolarityAudioProcessor&);
    ~WaveformPanel() override = default;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void setPlotLeftInset (int inset);

private:
    void timerCallback() override;
    float inputRmsAt (int point) const;
    float outputRmsAt (int point) const;

    PolarityAudioProcessor& processorRef;
    std::vector<float> frozenInput;
    std::vector<float> frozenOutput;
    int frozenWindowSamples = 0;
    int plotLeftInset = 0;
    bool frozen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformPanel)
};
