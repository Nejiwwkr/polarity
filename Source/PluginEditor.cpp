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
    setResizeLimits (920, 640, 1800, 1200);

    titleLabel.setText ("POLARITY", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (polarity::type::display)));
    titleLabel.setColour (juce::Label::textColourId, polarity::theme::ink);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("frequency inversion  /  inverted amount",
                           juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (polarity::type::primary)));
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    subtitleLabel.setColour (juce::Label::textColourId, polarity::theme::secondary);
    subtitleLabel.setMinimumHorizontalScale (1.0f);
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

    for (auto* button : { &slotAButton, &slotBButton })
    {
        styleButton (*button);
        button->setColour (juce::TextButton::buttonColourId,
                           juce::Colours::transparentBlack);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (104);
        addAndMakeVisible (*button);
    }

    slotAButton.onClick = [this]
    {
        processorRef.setActiveSlot (0);
        updateButtons();
    };

    slotBButton.onClick = [this]
    {
        processorRef.setActiveSlot (1);
        updateButtons();
    };

    for (auto* button : { &normButton, &deltaButton })
    {
        styleButton (*button);
        button->setColour (juce::TextButton::buttonColourId,
                           juce::Colours::transparentBlack);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (103);
        addAndMakeVisible (*button);
    }

    normButton.onClick = [this]
    {
        setDeltaOut (false);
        updateButtons();
    };

    deltaButton.onClick = [this]
    {
        setDeltaOut (true);
        updateButtons();
    };

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

    waveSpeedBox.addItem ("500 ms", 1);
    waveSpeedBox.addItem ("1 s", 2);
    waveSpeedBox.addItem ("2 s", 3);
    waveSpeedBox.addItem ("3 s", 4);
    waveSpeedBox.addItem ("5 s", 5);
    waveSpeedBox.setSelectedId (2, juce::dontSendNotification);
    styleCombo (waveSpeedBox);
    waveSpeedBox.onChange = [this]
    {
        static constexpr float windows[] = { 500.0f, 1000.0f, 2000.0f, 3000.0f, 5000.0f };
        const int index = juce::jlimit (0, 4, waveSpeedBox.getSelectedItemIndex());
        processorRef.setWaveformWindowMs (windows[index]);
        waveformPanel.setFrozen (false);
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

    metricCaption.setText ("METRIC", juce::dontSendNotification);
    metricCaption.setFont (juce::Font (juce::FontOptions (polarity::type::control)));
    metricCaption.setJustificationType (juce::Justification::centredLeft);
    metricCaption.setColour (juce::Label::textColourId, polarity::theme::secondary);
    addAndMakeVisible (metricCaption);

    styleCombo (metricBox);
    metricBox.addItem ("RMS", 1);
    metricBox.addItem ("PEAK", 2);

    const int storedMetric = static_cast<int> (
        processorRef.getValueTreeState().state.getProperty ("waveformMetric", 0));
    metricBox.setSelectedId (storedMetric == 1 ? 2 : 1,
                             juce::dontSendNotification);
    waveformPanel.setMetric (storedMetric == 1
                                 ? WaveformPanel::Metric::Peak
                                 : WaveformPanel::Metric::Rms);
    metricBox.onChange = [this]
    {
        const bool peak = metricBox.getSelectedItemIndex() == 1;
        waveformPanel.setMetric (peak ? WaveformPanel::Metric::Peak
                                      : WaveformPanel::Metric::Rms);
        processorRef.getValueTreeState().state.setProperty (
            "waveformMetric", peak ? 1 : 0, nullptr);
    };
    addAndMakeVisible (metricBox);

    qualityCaption.setText ("QUALITY", juce::dontSendNotification);
    qualityCaption.setFont (juce::Font (juce::FontOptions (polarity::type::control)));
    qualityCaption.setJustificationType (juce::Justification::centredLeft);
    qualityCaption.setColour (juce::Label::textColourId, polarity::theme::secondary);
    addAndMakeVisible (qualityCaption);

    styleCombo (qualityBox);
    refreshQualityItems();
    qualityBox.onChange = [this]
    {
        const int index = juce::jlimit (0, PolarityAudioProcessor::getQualityChoiceCount() - 1,
                                        qualityBox.getSelectedItemIndex());
        processorRef.setQuality (index);
    };
    addAndMakeVisible (qualityBox);

    updateButtons();
    startTimerHz (15);
}

void PolarityAudioProcessorEditor::timerCallback()
{
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

void PolarityAudioProcessorEditor::setDeltaOut (bool enable)
{
    if (auto* parameter = processorRef.getValueTreeState().getParameter ("deltaOut"))
        parameter->setValueNotifyingHost (enable ? 1.0f : 0.0f);
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

void PolarityAudioProcessorEditor::refreshQualityItems()
{
    const double rate = processorRef.getSampleRate() > 0.0
                            ? processorRef.getSampleRate()
                            : 48000.0;
    qualitySampleRate = rate;

    qualityBox.clear (juce::dontSendNotification);

    for (int i = 0; i < PolarityAudioProcessor::getQualityChoiceCount(); ++i)
    {
        const int taps = PolarityAudioProcessor::getQualityTaps (i);
        const double latencyMs = 1000.0 * (taps / 2) / rate;
        qualityBox.addItem (juce::String (taps) + " / "
                                + juce::String (latencyMs, 1) + " ms",
                            i + 1);
    }

    qualityBox.setSelectedId (processorRef.getQuality() + 1,
                              juce::dontSendNotification);
}

void PolarityAudioProcessorEditor::updateButtons()
{
    undoButton.setEnabled (curveEditor.canUndo());
    redoButton.setEnabled (curveEditor.canRedo());

    const bool slotA = processorRef.getActiveSlot() == 0;
    slotAButton.setToggleState (slotA, juce::dontSendNotification);
    slotBButton.setToggleState (! slotA, juce::dontSendNotification);

    const auto* deltaParameter = processorRef.getValueTreeState()
                                     .getRawParameterValue ("deltaOut");
    const bool deltaOn = deltaParameter != nullptr && deltaParameter->load() >= 0.5f;
    normButton.setToggleState (! deltaOn, juce::dontSendNotification);
    deltaButton.setToggleState (deltaOn, juce::dontSendNotification);

    const int metricValue = static_cast<int> (
        processorRef.getValueTreeState().state.getProperty ("waveformMetric", 0));
    const bool peakMetric = metricValue == 1;
    waveformPanel.setMetric (peakMetric ? WaveformPanel::Metric::Peak
                                        : WaveformPanel::Metric::Rms);

    if (metricBox.getSelectedId() != (peakMetric ? 2 : 1))
        metricBox.setSelectedId (peakMetric ? 2 : 1,
                                 juce::dontSendNotification);

    const double rate = processorRef.getSampleRate();
    if (rate > 0.0 && std::abs (rate - qualitySampleRate) > 1.0)
        refreshQualityItems();

    const int qualityId = processorRef.getQuality() + 1;
    if (qualityBox.getSelectedId() != qualityId)
        qualityBox.setSelectedId (qualityId, juce::dontSendNotification);

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
    g.fillRoundedRectangle (groupBounds ({ &slotAButton, &slotBButton }), 6.0f);
    g.fillRoundedRectangle (groupBounds ({ &layerLeft, &layerStereo, &layerRight }), 6.0f);
    g.fillRoundedRectangle (groupBounds ({ &toolDraw, &toolNode, &toolErase }), 6.0f);
    g.fillRoundedRectangle (groupBounds ({ &spectrumButton, &polarityButton }), 6.0f);
    g.fillRoundedRectangle (groupBounds ({ &normButton, &deltaButton }), 6.0f);
    g.fillRoundedRectangle (groupBounds ({ &qualityCaption, &qualityBox,
                                           &edgeCaption, &edgeBox }), 6.0f);
}

void PolarityAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto header = area.removeFromTop (34);
    titleLabel.setBounds (header.removeFromLeft (220).withTrimmedLeft (28).reduced (0, 2));

    auto headerRight = header;
    bypassToggle.setBounds (headerRight.removeFromRight (88).reduced (0, 4));
    headerRight.removeFromRight (8);
    deltaButton.setBounds (headerRight.removeFromRight (56).reduced (0, 4));
    normButton.setBounds (headerRight.removeFromRight (52).reduced (0, 4));
    headerRight.removeFromRight (8);
    savePresetButton.setBounds (headerRight.removeFromRight (56).reduced (0, 4));
    headerRight.removeFromRight (8);
    presetBox.setBounds (headerRight.removeFromRight (180).reduced (0, 4));

    area.removeFromTop (2);

    auto analyzerRow = area.removeFromTop (28);
    const int analyzerHeight = analyzerRow.getHeight();
    const int comboHeight = 24;
    const int comboPad = (analyzerHeight - comboHeight) / 2;

    auto analyzerControls = analyzerRow;

    const auto placeAnalyzer = [&] (juce::Label& label, juce::ComboBox& box,
                                    int labelWidth, int boxWidth)
    {
        box.setBounds (analyzerControls.removeFromRight (boxWidth)
                           .reduced (0, comboPad));
        label.setBounds (analyzerControls.removeFromRight (labelWidth)
                             .reduced (0, (analyzerHeight - 16) / 2));
    };

    placeAnalyzer (timeCaption, timeBox, 38, 84);
    analyzerControls.removeFromRight (8);
    placeAnalyzer (smoothCaption, smoothBox, 58, 90);
    analyzerControls.removeFromRight (8);
    placeAnalyzer (slopeCaption, slopeBox, 44, 90);

    subtitleLabel.setBounds (analyzerControls.withTrimmedLeft (28)
                                 .reduced (0, 4));

    area.removeFromTop (6);

    auto controls = area.removeFromTop (38);
    auto row = controls.reduced (0, 4);
    const int curveEditorRight = area.getRight() - 82 - 12;
    row.removeFromLeft (28);

    const auto takeLeft = [&] (int width)
    {
        auto r = row.removeFromLeft (width);
        return r;
    };

    slotAButton.setBounds (takeLeft (40));
    row.removeFromLeft (2);
    slotBButton.setBounds (takeLeft (40));
    row.removeFromLeft (14);

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

    row.removeFromLeft (18);
    undoButton.setBounds (takeLeft (54));
    row.removeFromLeft (4);
    redoButton.setBounds (takeLeft (54));

    // Anchor the spectrum toggles' plate to the spectrum panel's right edge.
    auto spectrumCluster = row.withRight (curveEditorRight - 3);
    polarityButton.setBounds (spectrumCluster.removeFromRight (84));
    spectrumCluster.removeFromRight (2);
    spectrumButton.setBounds (spectrumCluster.removeFromRight (90));

    area.removeFromTop (8);

    auto footer = area.removeFromBottom (34);
    area.removeFromBottom (10);

    const int waveformHeight = juce::jlimit (
        170, 210, juce::roundToInt (area.getHeight() * 0.30f));
    auto waveform = area.removeFromBottom (waveformHeight);
    area.removeFromBottom (12);

    auto meterArea = area.removeFromRight (82);
    area.removeFromRight (12);
    meterPanel.setBounds (meterArea);
    curveEditor.setBounds (area);
    waveformPanel.setBounds (waveform);

    auto waveHeader = waveformPanel.getHeaderBounds()
                          .translated (waveform.getX(), waveform.getY());
    auto waveHeaderRight = waveHeader;
    waveHeaderRight.removeFromRight (WaveformPanel::headerLegendWidth);
    waveHeaderRight.removeFromRight (12);
    metricBox.setBounds (waveHeaderRight.removeFromRight (84).reduced (0, 1));
    metricCaption.setBounds (waveHeaderRight.removeFromRight (48));
    waveHeaderRight.removeFromRight (14);
    waveSpeedBox.setBounds (waveHeaderRight.removeFromRight (96).reduced (0, 1));
    waveCaption.setBounds (waveHeaderRight.removeFromRight (42));

    auto footerControls = footer.reduced (0, 4);

    auto footerRight = footerControls.withTrimmedRight (3);
    edgeBox.setBounds (footerRight.removeFromRight (88).reduced (0, 1));
    edgeCaption.setBounds (footerRight.removeFromRight (40));
    footerRight.removeFromRight (12);
    qualityBox.setBounds (footerRight.removeFromRight (112).reduced (0, 1));
    qualityCaption.setBounds (footerRight.removeFromRight (56));

    auto footerLeft = footerControls;
    footerLeft.removeFromLeft (28);
    mixCaption.setBounds (footerLeft.removeFromLeft (36));
    const int sliderRight = juce::jmax (footerLeft.getX() + 100,
                                        footerRight.getRight() - 17);
    mixSlider.setBounds (footerLeft.withRight (sliderRight).reduced (0, 1));
}
