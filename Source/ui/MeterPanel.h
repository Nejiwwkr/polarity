#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class PolarityAudioProcessor;

class MeterPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit MeterPanel (PolarityAudioProcessor&);
    ~MeterPanel() override = default;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    static void drawBar (juce::Graphics&,
                         juce::Rectangle<float>,
                         float db,
                         juce::Colour);

    PolarityAudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterPanel)
};
