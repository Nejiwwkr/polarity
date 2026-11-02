#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/PolarityTheme.h"
#include "ui/PolarityType.h"

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
    setLookAndFeel (&lookAndFeel);

    setSize (960, 720);
    setResizable (true, true);
    setResizeLimits (820, 640, 1800, 1200);

    titleLabel.setText ("POLARITY", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (polarity::type::display)));
    titleLabel.setColour (juce::Label::textColourId, polarity::theme::ink);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("frequency inversion  /  inverted amount",
                           juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (polarity::type::primary)));
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    subtitleLabel.setColour (juce::Label::textColourId, polarity::theme::secondary);
    addAndMakeVisible (subtitleLabel);

    slopeCaption.setText ("SLOPE", juce::dontSendNotification);
    slopeCaption.setFont (juce::Font (juce::FontOptions (polarity::type::control)));
    slopeCaption.setJustificationType (juce::Justification::centredLeft);
    slopeCaption.setColour (juce::Label::textColourId, polarity::theme::secondary);
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
    smoothCaption.setFont (juce::Font (juce::FontOptions (polarity::type::control)));
    smoothCaption.setJustificationType (juce::Justification::centredLeft);
    smoothCaption.setColour (juce::Label::textColourId, polarity::theme::secondary);
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
    timeCaption.setFont (juce::Font (juce::FontOptions (polarity::type::control)));
    timeCaption.setJustificationType (juce::Justification::centredLeft);
    timeCaption.setColour (juce::Label::textColourId, polarity::theme::secondary);
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
    bypassToggle.setColour (juce::ToggleButton::textColourId, polarity::theme::ink);
    bypassToggle.setColour (juce::ToggleButton::tickColourId, polarity::theme::ink);
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
    savePresetButton.setColour (juce::TextButton::buttonColourId, polarity::theme::vermilion);
    savePresetButton.setColour (juce::TextButton::textColourOffId, polarity::theme::paper);
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
        button->setColour (juce::TextButton::buttonColourId,
                           juce::Colours::transparentBlack);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (101);
        addAndMakeVisible (*button);
    }

    for (auto* button : { &toolDraw, &toolNode, &toolErase })
    {
        styleButton (*button);
        button->setColour (juce::TextButton::buttonColourId,
                           juce::Colours::transparentBlack);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (102);
        addAndMakeVisible (*button);
    }

    styleButton (undoButton);
    styleButton (redoButton);

    for (auto* b : { &undoButton, &redoButton })
    {
        b->setColour (juce::TextButton::buttonColourId,
                      juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::textColourOffId,
                      polarity::theme::secondary);
    }

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
        button->setColour (juce::TextButton::buttonColourId,
                           juce::Colours::transparentBlack);
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
    mixCaption.setFont (juce::Font (juce::FontOptions (polarity::type::control)));
    mixCaption.setJustificationType (juce::Justification::centredLeft);
    mixCaption.setColour (juce::Label::textColourId, polarity::theme::secondary);
    addAndMakeVisible (mixCaption);

    mixSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    mixSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    mixSlider.setColour (juce::Slider::textBoxTextColourId, polarity::theme::ink);
    mixSlider.setColour (juce::Slider::textBoxBackgroundColourId, polarity::theme::paperBright);
    mixSlider.setColour (juce::Slider::textBoxOutlineColourId, polarity::theme::border);
    addAndMakeVisible (mixSlider);

    waveCaption.setText ("WAVE", juce::dontSendNotification);
    waveCaption.setFont (juce::Font (juce::FontOptions (polarity::type::control)));
    waveCaption.setJustificationType (juce::Justification::centredLeft);
    waveCaption.setColour (juce::Label::textColourId, polarity::theme::secondary);
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
    edgeCaption.setFont (juce::Font (juce::FontOptions (polarity::type::control)));
    edgeCaption.setJustificationType (juce::Justification::centredLeft);
    edgeCaption.setColour (juce::Label::textColourId, polarity::theme::secondary);
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
    statusLabel.setFont (juce::Font (juce::FontOptions (polarity::type::annotation)));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, polarity::theme::secondary);

    updateButtons();
}

PolarityAudioProcessorEditor::~PolarityAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void PolarityAudioProcessorEditor::styleButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, polarity::theme::panel);
    button.setColour (juce::TextButton::buttonOnColourId, polarity::theme::ink);
    button.setColour (juce::TextButton::textColourOffId, polarity::theme::ink);
    button.setColour (juce::TextButton::textColourOnId, polarity::theme::paper);
}

void PolarityAudioProcessorEditor::styleCombo (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::backgroundColourId, polarity::theme::paperBright);
    box.setColour (juce::ComboBox::textColourId, polarity::theme::ink);
    box.setColour (juce::ComboBox::outlineColourId, polarity::theme::border);
    box.setColour (juce::ComboBox::arrowColourId, polarity::theme::secondary);
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
    g.fillAll (polarity::theme::paper);

    g.setColour (polarity::theme::vermilion);
    g.fillRect (0, 0, 2, getHeight());
    g.drawLine (26.0f, 33.0f, 36.0f, 20.0f, 2.5f);

    const auto groupBounds = [] (std::initializer_list<juce::Component*> components)
    {
        juce::Rectangle<int> r;

        for (auto* component : components)
        {
            if (component == nullptr)
                continue;

            r = r.isEmpty() ? component->getBounds()
                            : r.getUnion (component->getBounds());
        }

        return r.expanded (3, 3).toFloat();
    };

    g.setColour (polarity::theme::panelAlt);
    g.fillRoundedRectangle (groupBounds ({ &layerLeft, &layerStereo, &layerRight }), 6.0f);
    g.fillRoundedRectangle (groupBounds ({ &toolDraw, &toolNode, &toolErase }), 6.0f);
    g.fillRoundedRectangle (groupBounds ({ &spectrumButton, &polarityButton }), 6.0f);
}

void PolarityAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto header = area.removeFromTop (34);
    titleLabel.setBounds (header.removeFromLeft (220).withTrimmedLeft (28).reduced (0, 2));

    auto headerRight = header;
    bypassToggle.setBounds (headerRight.removeFromRight (88).reduced (0, 4));
    headerRight.removeFromRight (8);
    savePresetButton.setBounds (headerRight.removeFromRight (56).reduced (0, 4));
    headerRight.removeFromRight (8);
    presetBox.setBounds (headerRight.removeFromRight (180).reduced (0, 4));

    area.removeFromTop (2);

    auto analyzerRow = area.removeFromTop (28);
    subtitleLabel.setBounds (analyzerRow.removeFromLeft (340)
                                 .withTrimmedLeft (28)
                                 .reduced (0, 4));

    auto analyzerControls = analyzerRow;
    const int comboHeight = 24;

    const auto placeAnalyzer = [&] (juce::Label& label, juce::ComboBox& box,
                                    int labelWidth, int boxWidth)
    {
        box.setBounds (analyzerControls.removeFromRight (boxWidth)
                           .reduced (0, (28 - comboHeight) / 2));
        label.setBounds (analyzerControls.removeFromRight (labelWidth)
                             .reduced (0, (28 - 16) / 2));
    };

    placeAnalyzer (timeCaption, timeBox, 38, 88);
    analyzerControls.removeFromRight (8);
    placeAnalyzer (smoothCaption, smoothBox, 58, 94);
    analyzerControls.removeFromRight (8);
    placeAnalyzer (slopeCaption, slopeBox, 44, 94);

    area.removeFromTop (6);

    auto controls = area.removeFromTop (38);
    auto row = controls.reduced (0, 4);
    row.removeFromRight (3);
    row.removeFromLeft (28);

    const auto takeLeft = [&] (int width)
    {
        auto r = row.removeFromLeft (width);
        return r;
    };

    const auto takeRight = [&] (int width)
    {
        auto r = row.removeFromRight (width);
        return r;
    };

    polarityButton.setBounds (takeRight (84));
    row.removeFromRight (2);
    spectrumButton.setBounds (takeRight (90));
    row.removeFromRight (18);
    redoButton.setBounds (takeRight (54));
    row.removeFromRight (4);
    undoButton.setBounds (takeRight (54));

    abButton.setBounds (takeLeft (68));
    row.removeFromLeft (10);

    layerLeft.setBounds (takeLeft (40));
    row.removeFromLeft (2);
    layerStereo.setBounds (takeLeft (46));
    row.removeFromLeft (2);
    layerRight.setBounds (takeLeft (40));

    row.removeFromLeft (26);

    toolDraw.setBounds (takeLeft (60));
    row.removeFromLeft (2);
    toolNode.setBounds (takeLeft (60));
    row.removeFromLeft (2);
    toolErase.setBounds (takeLeft (64));

    area.removeFromTop (8);

    auto footer = area.removeFromBottom (34);
    area.removeFromBottom (10);

    const int waveformHeight = juce::jlimit (
        140, 180, juce::roundToInt (area.getHeight() * 0.28f));
    auto waveform = area.removeFromBottom (waveformHeight);
    const int waveformRight = waveform.getRight();
    area.removeFromBottom (12);

    auto meterArea = area.removeFromRight (82);
    area.removeFromRight (12);
    meterPanel.setBounds (meterArea);
    curveEditor.setBounds (area);

    const int curvePlotLeft = curveEditor.getX() + 4 + 24;
    waveform.setX (juce::jmax (waveform.getX(), curvePlotLeft - 8));
    waveform.setWidth (waveformRight - waveform.getX());
    waveformPanel.setBounds (waveform);
    waveformPanel.setPlotLeftInset (0);

    auto footerControls = footer.reduced (0, 4);
    footerControls.removeFromLeft (28);
    mixCaption.setBounds (footerControls.removeFromLeft (36));
    mixSlider.setBounds (footerControls.removeFromLeft (210).reduced (0, 1));

    auto footerRight = footerControls;
    edgeBox.setBounds (footerRight.removeFromRight (88).reduced (0, 1));
    edgeCaption.setBounds (footerRight.removeFromRight (40));
    footerRight.removeFromRight (14);
    waveSpeedBox.setBounds (footerRight.removeFromRight (96).reduced (0, 1));
    waveCaption.setBounds (footerRight.removeFromRight (42));
}
