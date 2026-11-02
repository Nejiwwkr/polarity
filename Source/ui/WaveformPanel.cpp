#include "WaveformPanel.h"

#include "../PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
const juce::Colour kPanel  { 0xff0f1216 };
const juce::Colour kGrid   { 0xff20262f };
const juce::Colour kDim    { 0xff737b88 };
const juce::Colour kInput  { 0x99a7c7d6 };
const juce::Colour kOutput { 0xff8fe8ff };
const juce::Colour kWarning { 0xffffcc66 };
} // namespace

WaveformPanel::WaveformPanel (PolarityAudioProcessor& processor)
    : processorRef (processor)
{
    setInterceptsMouseClicks (true, true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

void WaveformPanel::timerCallback()
{
    repaint();
}

void WaveformPanel::mouseDown (const juce::MouseEvent&)
{
    if (! frozen)
    {
        const int points = PolarityAudioProcessor::getWaveformPointCount();
        frozenInput.resize (static_cast<std::size_t> (points));
        frozenOutput.resize (static_cast<std::size_t> (points));

        for (int i = 0; i < points; ++i)
        {
            frozenInput[static_cast<std::size_t> (i)]
                = processorRef.getWaveformInputRms (i);
            frozenOutput[static_cast<std::size_t> (i)]
                = processorRef.getWaveformOutputRms (i);
        }

        frozenWindowSamples = processorRef.getWaveformWindowSamples();
        frozen = true;
    }
    else
    {
        frozen = false;
    }

    repaint();
}

float WaveformPanel::inputRmsAt (int point) const
{
    if (frozen && point >= 0 && point < static_cast<int> (frozenInput.size()))
        return frozenInput[static_cast<std::size_t> (point)];

    return processorRef.getWaveformInputRms (point);
}

float WaveformPanel::outputRmsAt (int point) const
{
    if (frozen && point >= 0 && point < static_cast<int> (frozenOutput.size()))
        return frozenOutput[static_cast<std::size_t> (point)];

    return processorRef.getWaveformOutputRms (point);
}

void WaveformPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111318));

    auto area = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (juce::Colour (0xff171a20));
    g.fillRoundedRectangle (area, 5.0f);

    auto inner = area.reduced (6.0f);
    auto header = inner.removeFromTop (12.0f);

    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.setColour (frozen ? kWarning : kDim);
    g.drawText ("WAVEFORM  /  RMS", header.toNearestInt(),
                juce::Justification::left, false);

    g.setFont (juce::Font (juce::FontOptions (8.0f)));
    g.setColour (frozen ? kWarning : kDim.withAlpha (0.55f));
    g.drawText (frozen ? "FROZEN - click to resume" : "click to freeze",
                header.withTrimmedLeft (110.0f).withTrimmedRight (110.0f)
                    .toNearestInt(),
                juce::Justification::centred,
                false);

    const double sampleRate = processorRef.getSampleRate();
    const int windowSamples = frozen ? frozenWindowSamples
                                     : processorRef.getWaveformWindowSamples();
    const int windowMs = sampleRate > 0.0
                             ? juce::roundToInt (1000.0 * windowSamples / sampleRate)
                             : 0;

    auto rightArea = header;
    g.setColour (kOutput);
    g.drawText ("OUT", rightArea.removeFromRight (30.0f).toNearestInt(),
                juce::Justification::centredRight, false);
    g.setColour (kInput);
    g.drawText ("IN", rightArea.removeFromRight (24.0f).toNearestInt(),
                juce::Justification::centredRight, false);
    g.setColour (kDim);
    g.drawText (windowMs > 0 ? juce::String (windowMs) + " ms" : juce::String(),
                rightArea.toNearestInt(), juce::Justification::centredRight, false);

    inner.removeFromTop (2.0f);

    auto plot = inner;
    g.setColour (kPanel);
    g.fillRoundedRectangle (plot, 3.0f);

    const float centre = plot.getCentreY();
    g.setColour (kGrid);
    g.drawHorizontalLine (juce::roundToInt (centre), plot.getX(), plot.getRight());

    const int points = PolarityAudioProcessor::getWaveformPointCount();
    const float scale = plot.getHeight() * 0.45f;
    const auto rmsToNormalised = [] (float rms)
    {
        const float db = 20.0f * std::log10 (std::max (rms, 1.0e-6f));
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };

    const auto xAt = [&plot, points] (int index)
    {
        return plot.getX()
               + (points > 1 ? static_cast<float> (index)
                                   / static_cast<float> (points - 1)
                             : 0.0f)
                     * plot.getWidth();
    };

    juce::Path inputEnvelope;
    juce::Path outputEnvelope;

    // RMS envelope top edge, left to right
    for (int i = 0; i < points; ++i)
    {
        const float x = xAt (i);
        const float inputTop = centre
                               - rmsToNormalised (inputRmsAt (i))
                                     * scale;
        const float outputTop = centre
                                - rmsToNormalised (outputRmsAt (i))
                                      * scale;

        if (i == 0)
        {
            inputEnvelope.startNewSubPath (x, inputTop);
            outputEnvelope.startNewSubPath (x, outputTop);
        }
        else
        {
            inputEnvelope.lineTo (x, inputTop);
            outputEnvelope.lineTo (x, outputTop);
        }
    }

    // RMS envelope bottom edge, right to left
    for (int i = points - 1; i >= 0; --i)
    {
        const float x = xAt (i);
        const float inputBottom = centre
                                  + rmsToNormalised (inputRmsAt (i))
                                        * scale;
        const float outputBottom = centre
                                   + rmsToNormalised (outputRmsAt (i))
                                         * scale;

        inputEnvelope.lineTo (x, inputBottom);
        outputEnvelope.lineTo (x, outputBottom);
    }

    inputEnvelope.closeSubPath();
    outputEnvelope.closeSubPath();

    g.setColour (kInput.withAlpha (0.10f));
    g.fillPath (inputEnvelope);

    g.setColour (kOutput.withAlpha (0.22f));
    g.fillPath (outputEnvelope);

    g.setColour (kInput.withAlpha (0.80f));
    g.strokePath (inputEnvelope, juce::PathStrokeType (1.0f));

    g.setColour (kOutput);
    g.strokePath (outputEnvelope,
                  juce::PathStrokeType (1.2f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
}
