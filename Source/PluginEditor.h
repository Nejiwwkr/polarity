#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/PresetManager.h"
#include "ui/WaveformPanel.h"
#include "ui/CurveEditor.h"
#include "ui/PolarityLookAndFeel.h"
#include "ui/MeterPanel.h"

class PolarityAudioProcessor;

class PolarityAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit PolarityAudioProcessorEditor (PolarityAudioProcessor&);
    ~PolarityAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateButtons();
    void timerCallback() override;
    void refreshPresetList();
    void refreshQualityItems();
    void setDeltaOut (bool enable);
    static void styleButton (juce::TextButton&);
    static void styleCombo (juce::ComboBox&);

    PolarityAudioProcessor& processorRef;
    PolarityLookAndFeel lookAndFeel;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label slopeCaption;
    juce::ComboBox slopeBox;
    juce::Label smoothCaption;
    juce::ComboBox smoothBox;
    juce::Label timeCaption;
    juce::ComboBox timeBox;
    juce::Label edgeCaption;
    juce::ComboBox edgeBox;

    juce::ToggleButton bypassToggle;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    PresetManager presetManager;
    juce::ComboBox presetBox;
    juce::TextButton savePresetButton { "SAVE" };
    bool suppressPresetLoad = false;

    juce::TextButton slotAButton { "A" };
    juce::TextButton slotBButton { "B" };
    juce::TextButton normButton { "NORM" };
    juce::TextButton deltaButton { "DELTA" };
    juce::TextButton layerLeft { "L" };
    juce::TextButton layerStereo { "ST" };
    juce::TextButton layerRight { "R" };
    juce::TextButton toolDraw { "DRAW" };
    juce::TextButton toolNode { "NODE" };
    juce::TextButton toolErase { "ERASE" };
    juce::TextButton undoButton { "UNDO" };
    juce::TextButton redoButton { "REDO" };
    juce::TextButton spectrumButton { "SPECTRUM" };
    juce::TextButton polarityButton { "POLARITY" };

    CurveEditor curveEditor;
    MeterPanel meterPanel;
    WaveformPanel waveformPanel;

    juce::Label mixCaption;
    juce::Slider mixSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment mixAttachment;
    juce::Label waveCaption;
    juce::ComboBox waveSpeedBox;
    juce::Label metricCaption;
    juce::ComboBox metricBox;
    juce::Label qualityCaption;
    juce::ComboBox qualityBox;
    double qualitySampleRate = 0.0;

    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolarityAudioProcessorEditor)
};
