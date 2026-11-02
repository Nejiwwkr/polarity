#include "PluginEditor.h"
#include "PluginProcessor.h"

PolarityAudioProcessorEditor::PolarityAudioProcessorEditor (PolarityAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      bypassAttachment (p.getValueTreeState(), "bypass", bypassToggle),
      presetManager (p),
      curveEditor (p),
      meterPanel (p),
      waveformPanel (p),
      mixAttachment (p.getValueTreeState(), "mix", mixSlider)
{
    setSize (960, 720);
    setResizable (true, true);
    setResizeLimits (820, 640, 1800, 1200);

    titleLabel.setText ("POLARITY  -  frequency inversion",
                        juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (20.0f)));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xfff2f4f8));
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("draw how much inverted signal to add per band",
                           juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    subtitleLabel.setColour (juce::Label::textColourId, juce::Colour (0xff7f8794));
    addAndMakeVisible (subtitleLabel);

    slopeCaption.setText ("SLOPE", juce::dontSendNotification);
    slopeCaption.setFont (juce::Font (juce::FontOptions (11.0f)));
    slopeCaption.setJustificationType (juce::Justification::centredLeft);
    slopeCaption.setColour (juce::Label::textColourId, juce::Colour (0xff7f8794));
    addAndMakeVisible (slopeCaption);

    styleCombo (slopeBox);
    slopeBox.addItem ("Flat", 1);
    slopeBox.addItem ("3 dB/oct", 2);
    slopeBox.addItem ("4.5 dB/oct", 3);
    slopeBox.addItem ("6 dB/oct", 4);
    slopeBox.setSelectedId (3, juce::dontSendNotification);
    slopeBox.onChange = [this]
    {
        static constexpr float slopes[] = { 0.0f, 3.0f, 4.5f, 6.0f };
        const int index = juce::jlimit (0, 3, slopeBox.getSelectedItemIndex());
        curveEditor.setSpectrumSlope (slopes[index]);
    };
    addAndMakeVisible (slopeBox);

    smoothCaption.setText ("SMOOTH", juce::dontSendNotification);
    smoothCaption.setFont (juce::Font (juce::FontOptions (11.0f)));
    smoothCaption.setJustificationType (juce::Justification::centredLeft);
    smoothCaption.setColour (juce::Label::textColourId, juce::Colour (0xff7f8794));
    addAndMakeVisible (smoothCaption);

    styleCombo (smoothBox);
    smoothBox.addItem ("Off", 1);
    smoothBox.addItem ("1/12 oct", 2);
    smoothBox.addItem ("1/6 oct", 3);
    smoothBox.addItem ("1/3 oct", 4);
    smoothBox.setSelectedId (2, juce::dontSendNotification);
    smoothBox.onChange = [this]
    {
        static constexpr int radii[] = { 0, 2, 4, 8 };
        const int index = juce::jlimit (0, 3, smoothBox.getSelectedItemIndex());
        processorRef.setSpectrumSmoothing (radii[index]);
    };
    addAndMakeVisible (smoothBox);

    timeCaption.setText ("TIME", juce::dontSendNotification);
    timeCaption.setFont (juce::Font (juce::FontOptions (11.0f)));
    timeCaption.setJustificationType (juce::Justification::centredLeft);
    timeCaption.setColour (juce::Label::textColourId, juce::Colour (0xff7f8794));
    addAndMakeVisible (timeCaption);

    styleCombo (timeBox);
    timeBox.addItem ("50 ms", 1);
    timeBox.addItem ("100 ms", 2);
    timeBox.addItem ("200 ms", 3);
    timeBox.addItem ("400 ms", 4);
    timeBox.setSelectedId (3, juce::dontSendNotification);
    timeBox.onChange = [this]
    {
        static constexpr float times[] = { 50.0f, 100.0f, 200.0f, 400.0f };
        const int index = juce::jlimit (0, 3, timeBox.getSelectedItemIndex());
        processorRef.setSpectrumTimeSmoothing (times[index]);
    };
    addAndMakeVisible (timeBox);

    bypassToggle.setButtonText ("BYPASS");
    bypassToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (0xff9aa2ae));
    bypassToggle.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff4da3ff));
    addAndMakeVisible (bypassToggle);

    presetBox.setEditableText (true);
    presetBox.setTextWhenNothingSelected ("preset...");
    presetBox.onChange = [this]
    {
        if (suppressPresetLoad)
            return;

        const auto name = presetBox.getText().trim();

        if (name.isNotEmpty())
            presetManager.loadPreset (name);
    };
    addAndMakeVisible (presetBox);

    styleButton (savePresetButton);
    savePresetButton.onClick = [this]
    {
        auto name = presetBox.getText().trim();

        if (name.isEmpty())
            name = "Preset " + juce::String (presetManager.getPresetNames().size() + 1);

        const auto safeName = PresetManager::sanitiseName (name);

        if (presetManager.savePreset (safeName))
        {
            refreshPresetList();
            suppressPresetLoad = true;
            presetBox.setText (safeName, juce::dontSendNotification);
            suppressPresetLoad = false;
        }
    };
    addAndMakeVisible (savePresetButton);

    refreshPresetList();

    styleButton (abButton);
    abButton.onClick = [this]
    {
        processorRef.setActiveSlot (1 - processorRef.getActiveSlot());
        updateButtons();
    };
    addAndMakeVisible (abButton);

    for (auto* button : { &layerLeft, &layerStereo, &layerRight })
    {
        styleButton (*button);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (101);
        addAndMakeVisible (*button);
    }

    for (auto* button : { &toolDraw, &toolNode, &toolErase })
    {
        styleButton (*button);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (102);
        addAndMakeVisible (*button);
    }

    styleButton (undoButton);
    styleButton (redoButton);
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    layerLeft.onClick = [this]
    {
        curveEditor.setLayer (CurveEditor::Layer::Left);
        updateButtons();
    };

    layerStereo.onClick = [this]
    {
        curveEditor.setLayer (CurveEditor::Layer::Stereo);
        updateButtons();
    };

    layerRight.onClick = [this]
    {
        curveEditor.setLayer (CurveEditor::Layer::Right);
        updateButtons();
    };

    toolDraw.onClick = [this]
    {
        curveEditor.setTool (CurveEditor::Tool::Draw);
        updateButtons();
    };

    toolNode.onClick = [this]
    {
        curveEditor.setTool (CurveEditor::Tool::Node);
        updateButtons();
    };

    toolErase.onClick = [this]
    {
        curveEditor.setTool (CurveEditor::Tool::Erase);
        updateButtons();
    };

    undoButton.onClick = [this]
    {
        curveEditor.undo();
        updateButtons();
    };

    redoButton.onClick = [this]
    {
        curveEditor.redo();
        updateButtons();
    };

    for (auto* button : { &spectrumButton, &polarityButton })
    {
        styleButton (*button);
        button->setClickingTogglesState (true);
        button->setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (*button);
    }

    spectrumButton.onClick = [this]
    {
        curveEditor.setSpectrumVisible (spectrumButton.getToggleState());
        updateButtons();
    };

    polarityButton.onClick = [this]
    {
        curveEditor.setPolarityVisible (polarityButton.getToggleState());
        updateButtons();
    };

    curveEditor.onEdit = [this] { updateButtons(); };
    addAndMakeVisible (curveEditor);
    addAndMakeVisible (meterPanel);
    addAndMakeVisible (waveformPanel);

    mixCaption.setText ("MIX", juce::dontSendNotification);
    mixCaption.setFont (juce::Font (juce::FontOptions (12.0f)));
    mixCaption.setJustificationType (juce::Justification::centredLeft);
    mixCaption.setColour (juce::Label::textColourId, juce::Colour (0xff7f8794));
    addAndMakeVisible (mixCaption);

    mixSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    mixSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    mixSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff4da3ff));
    mixSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252a33));
    mixSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffdfe6f0));
    addAndMakeVisible (mixSlider);

    waveCaption.setText ("WAVE", juce::dontSendNotification);
    waveCaption.setFont (juce::Font (juce::FontOptions (12.0f)));
    waveCaption.setJustificationType (juce::Justification::centredLeft);
    waveCaption.setColour (juce::Label::textColourId, juce::Colour (0xff7f8794));
    addAndMakeVisible (waveCaption);

    waveSpeedBox.addItem ("Fast", 1);
    waveSpeedBox.addItem ("Normal", 2);
    waveSpeedBox.addItem ("Slow", 3);
    waveSpeedBox.addItem ("Very slow", 4);
    waveSpeedBox.addItem ("Extreme", 5);
    waveSpeedBox.addItem ("Ultra", 6);
    waveSpeedBox.setSelectedId (5, juce::dontSendNotification);
    styleCombo (waveSpeedBox);
    waveSpeedBox.onChange = [this]
    {
        static constexpr int samples[] = { 4096, 8192, 16384, 32768, 65536, 131072 };
        const int index = juce::jlimit (0, 5, waveSpeedBox.getSelectedItemIndex());
        processorRef.setWaveformWindow (samples[index]);
    };
    addAndMakeVisible (waveSpeedBox);

    edgeCaption.setText ("EDGE", juce::dontSendNotification);
    edgeCaption.setFont (juce::Font (juce::FontOptions (12.0f)));
    edgeCaption.setJustificationType (juce::Justification::centredLeft);
    edgeCaption.setColour (juce::Label::textColourId, juce::Colour (0xff7f8794));
    addAndMakeVisible (edgeCaption);

    styleCombo (edgeBox);
    edgeBox.addItem ("Off", 1);
    edgeBox.addItem ("1 bin", 2);
    edgeBox.addItem ("2 bins", 3);
    edgeBox.addItem ("4 bins", 4);
    edgeBox.setSelectedId (3, juce::dontSendNotification);
    edgeBox.onChange = [this]
    {
        static constexpr int bins[] = { 0, 1, 2, 4 };
        const int index = juce::jlimit (0, 3, edgeBox.getSelectedItemIndex());
        processorRef.setCurveSmoothing (bins[index]);
    };
    addAndMakeVisible (edgeBox);

    statusLabel.setText (
        "heatmap: 0 blue / NULL light / INV red",
        juce::dontSendNotification);
    statusLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff6f7782));
    addAndMakeVisible (statusLabel);

    updateButtons();
}

void PolarityAudioProcessorEditor::styleButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b2028));
    button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2e6fbd));
    button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff9aa2ae));
    button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void PolarityAudioProcessorEditor::styleCombo (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1b2028));
    box.setColour (juce::ComboBox::textColourId, juce::Colour (0xff9aa2ae));
    box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a2f38));
    box.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff7f8794));
}

void PolarityAudioProcessorEditor::refreshPresetList()
{
    suppressPresetLoad = true;
    presetBox.clear (juce::dontSendNotification);

    const auto names = presetManager.getPresetNames();

    for (int i = 0; i < names.size(); ++i)
        presetBox.addItem (names[i], i + 1);

    suppressPresetLoad = false;
}

void PolarityAudioProcessorEditor::updateButtons()
{
    undoButton.setEnabled (curveEditor.canUndo());
    redoButton.setEnabled (curveEditor.canRedo());

    abButton.setButtonText (processorRef.getActiveSlot() == 0 ? "A/B: A" : "A/B: B");

    spectrumButton.setToggleState (curveEditor.isSpectrumVisible(),
                                   juce::dontSendNotification);
    polarityButton.setToggleState (curveEditor.isPolarityVisible(),
                                   juce::dontSendNotification);

    const auto activeLayer = curveEditor.getLayer();
    layerLeft.setToggleState (activeLayer == CurveEditor::Layer::Left,
                              juce::dontSendNotification);
    layerStereo.setToggleState (activeLayer == CurveEditor::Layer::Stereo,
                                juce::dontSendNotification);
    layerRight.setToggleState (activeLayer == CurveEditor::Layer::Right,
                               juce::dontSendNotification);

    const auto activeTool = curveEditor.getTool();
    toolDraw.setToggleState (activeTool == CurveEditor::Tool::Draw,
                             juce::dontSendNotification);
    toolNode.setToggleState (activeTool == CurveEditor::Tool::Node,
                             juce::dontSendNotification);
    toolErase.setToggleState (activeTool == CurveEditor::Tool::Erase,
                              juce::dontSendNotification);
}

void PolarityAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0f13));
    g.setColour (juce::Colour (0xff242832));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (8.0f), 10.0f, 1.0f);
}

void PolarityAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto titleRow = area.removeFromTop (28);
    titleLabel.setBounds (titleRow.removeFromLeft (230));
    titleRow.removeFromLeft (6);
    presetBox.setBounds (titleRow.removeFromLeft (170).reduced (0, 2));
    titleRow.removeFromLeft (6);
    savePresetButton.setBounds (titleRow.removeFromLeft (52).reduced (0, 2));
    bypassToggle.setBounds (titleRow.removeFromRight (84).reduced (0, 1));

    auto subtitleRow = area.removeFromTop (24);
    subtitleLabel.setBounds (subtitleRow.removeFromLeft (300));

    auto analyzerControls = subtitleRow;
    slopeCaption.setBounds (analyzerControls.removeFromLeft (40));
    slopeBox.setBounds (analyzerControls.removeFromLeft (90).reduced (0, 2));
    analyzerControls.removeFromLeft (6);
    smoothCaption.setBounds (analyzerControls.removeFromLeft (54));
    smoothBox.setBounds (analyzerControls.removeFromLeft (84).reduced (0, 2));
    analyzerControls.removeFromLeft (6);
    timeCaption.setBounds (analyzerControls.removeFromLeft (36));
    timeBox.setBounds (analyzerControls.removeFromLeft (76).reduced (0, 2));

    area.removeFromTop (4);

    auto controls = area.removeFromTop (30);
    abButton.setBounds (controls.removeFromLeft (66));
    controls.removeFromLeft (8);

    layerLeft.setBounds (controls.removeFromLeft (40));
    controls.removeFromLeft (2);
    layerStereo.setBounds (controls.removeFromLeft (44));
    controls.removeFromLeft (2);
    layerRight.setBounds (controls.removeFromLeft (40));

    controls.removeFromLeft (18);
    toolDraw.setBounds (controls.removeFromLeft (56));
    controls.removeFromLeft (2);
    toolNode.setBounds (controls.removeFromLeft (56));
    controls.removeFromLeft (2);
    toolErase.setBounds (controls.removeFromLeft (60));

    controls.removeFromLeft (18);
    undoButton.setBounds (controls.removeFromLeft (56));
    controls.removeFromLeft (2);
    redoButton.setBounds (controls.removeFromLeft (56));

    controls.removeFromLeft (14);
    spectrumButton.setBounds (controls.removeFromLeft (84));
    controls.removeFromLeft (2);
    polarityButton.setBounds (controls.removeFromLeft (78));

    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (34);
    mixCaption.setBounds (bottom.removeFromLeft (38));
    mixSlider.setBounds (bottom.removeFromLeft (210).reduced (0, 5));
    bottom.removeFromLeft (12);
    waveCaption.setBounds (bottom.removeFromLeft (44));
    waveSpeedBox.setBounds (bottom.removeFromLeft (92).reduced (0, 4));
    bottom.removeFromLeft (12);
    edgeCaption.setBounds (bottom.removeFromLeft (40));
    edgeBox.setBounds (bottom.removeFromLeft (76).reduced (0, 4));
    statusLabel.setBounds (bottom);

    const int waveformHeight = juce::jlimit (130, 190,
                                             juce::roundToInt (area.getHeight() * 0.28f));
    auto waveform = area.removeFromBottom (waveformHeight);
    area.removeFromBottom (6);

    auto meterArea = area.removeFromRight (86);
    area.removeFromRight (6);
    meterPanel.setBounds (meterArea.reduced (0, 2));
    curveEditor.setBounds (area);
    waveformPanel.setBounds (waveform);
}
