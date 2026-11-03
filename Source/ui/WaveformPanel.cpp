#include "WaveformPanel.h"

#include "../PluginProcessor.h"
#include "PolarityTheme.h"
#include "PolarityType.h"

#include <algorithm>
#include <cmath>

namespace
{
const juce::Colour kPanel  = polarity::theme::paperBright;
const juce::Colour kGrid   = polarity::theme::grid;
const juce::Colour kDim    = polarity::theme::secondary;
const juce::Colour kInput  { 0xff8fa1ae };
const juce::Colour kOutput = polarity::theme::teal;
const juce::Colour kWarning = polarity::theme::amber;

constexpr float kOuterInset = 8.0f;
constexpr float kLeftGutter = 20.0f;
constexpr float kInnerInset = 6.0f;
constexpr float kInnerLeftTrim = 6.0f;
constexpr float kHeaderHeight = 26.0f;

juce::Rectangle<float> innerPanelArea (juce::Rectangle<float> local)
{
    return local.reduced (kOuterInset)
                .withTrimmedLeft (kLeftGutter)
                .reduced (kInnerInset)
                .withTrimmedLeft (kInnerLeftTrim);
}
} // namespace

WaveformPanel::WaveformPanel (PolarityAudioProcessor& processor)
    : processorRef (processor)
{
    setTooltip ("Click to freeze the waveform; click again to resume");
    setInterceptsMouseClicks (true, true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

void WaveformPanel::timerCallback()
{
    repaint();
}

void WaveformPanel::setFrozen (bool shouldFreeze)
{
    if (frozen == shouldFreeze)
        return;

    frozen = shouldFreeze;
    repaint();
}

void WaveformPanel::setMetric (Metric newMetric)
{
    if (metric == newMetric)
        return;

    metric = newMetric;
    repaint();
}

void WaveformPanel::mouseDown (const juce::MouseEvent&)
{
    if (! frozen)
    {
        const int points = PolarityAudioProcessor::getWaveformPointCount();
        frozenInputRms.resize (static_cast<std::size_t> (points));
        frozenOutputRms.resize (static_cast<std::size_t> (points));
        frozenInputMin.resize (static_cast<std::size_t> (points));
        frozenInputMax.resize (static_cast<std::size_t> (points));
        frozenOutputMin.resize (static_cast<std::size_t> (points));
        frozenOutputMax.resize (static_cast<std::size_t> (points));

        for (int i = 0; i < points; ++i)
        {
            const auto index = static_cast<std::size_t> (i);
            frozenInputRms[index] = processorRef.getWaveformInputRms (i);
            frozenOutputRms[index] = processorRef.getWaveformOutputRms (i);
            frozenInputMin[index] = processorRef.getWaveformInputMin (i);
            frozenInputMax[index] = processorRef.getWaveformInputMax (i);
            frozenOutputMin[index] = processorRef.getWaveformOutputMin (i);
            frozenOutputMax[index] = processorRef.getWaveformOutputMax (i);
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
    if (frozen && point >= 0 && point < static_cast<int> (frozenInputRms.size()))
        return frozenInputRms[static_cast<std::size_t> (point)];

    return processorRef.getWaveformInputRms (point);
}

float WaveformPanel::outputRmsAt (int point) const
{
    if (frozen && point >= 0 && point < static_cast<int> (frozenOutputRms.size()))
        return frozenOutputRms[static_cast<std::size_t> (point)];

    return processorRef.getWaveformOutputRms (point);
}

float WaveformPanel::inputPeakAt (int point) const
{
    const auto index = static_cast<std::size_t> (
        juce::jlimit (0, PolarityAudioProcessor::getWaveformPointCount() - 1, point));
    const float mn = frozen && index < frozenInputMin.size()
                         ? frozenInputMin[index]
                         : processorRef.getWaveformInputMin (point);
    const float mx = frozen && index < frozenInputMax.size()
                         ? frozenInputMax[index]
                         : processorRef.getWaveformInputMax (point);

    return std::max (std::abs (mn), std::abs (mx));
}

float WaveformPanel::outputPeakAt (int point) const
{
    const auto index = static_cast<std::size_t> (
        juce::jlimit (0, PolarityAudioProcessor::getWaveformPointCount() - 1, point));
    const float mn = frozen && index < frozenOutputMin.size()
                         ? frozenOutputMin[index]
                         : processorRef.getWaveformOutputMin (point);
    const float mx = frozen && index < frozenOutputMax.size()
                         ? frozenOutputMax[index]
                         : processorRef.getWaveformOutputMax (point);

    return std::max (std::abs (mn), std::abs (mx));
}

juce::Rectangle<int> WaveformPanel::getHeaderBounds() const
{
    return innerPanelArea (getLocalBounds().toFloat())
        .removeFromTop (kHeaderHeight)
        .toNearestInt();
}

void WaveformPanel::paint (juce::Graphics& g)
{
    g.fillAll (polarity::theme::paper);

    auto area = getLocalBounds().toFloat()
                    .reduced (kOuterInset)
                    .withTrimmedLeft (kLeftGutter);

    juce::DropShadow shadow (juce::Colour (0x12000000), 6, { 0, 2 });
    juce::Path panelPath;
    panelPath.addRoundedRectangle (area, 8.0f);
    shadow.drawForPath (g, panelPath);

    g.setColour (polarity::theme::panel);
    g.fillRoundedRectangle (area, 8.0f);

    auto inner = innerPanelArea (getLocalBounds().toFloat());
    auto header = inner.removeFromTop (kHeaderHeight);

    const double sampleRate = processorRef.getSampleRate();
    const int windowSamples = frozen ? frozenWindowSamples
                                     : processorRef.getWaveformWindowSamples();
    const int windowMs = sampleRate > 0.0
                             ? juce::roundToInt (1000.0 * windowSamples / sampleRate)
                             : 0;

    g.setFont (juce::Font (juce::FontOptions (polarity::type::annotation)));
    g.setColour (frozen ? kWarning : kDim);
    g.drawText ("WAVEFORM",
                header.toNearestInt(),
                juce::Justification::left, false);

    g.setColour (frozen ? kWarning : kDim.withAlpha (0.55f));
    g.drawText (frozen ? "FROZEN - click to resume" : "click to freeze",
                header.withTrimmedLeft (110.0f).withWidth (180.0f)
                    .toNearestInt(),
                juce::Justification::left,
                false);

    auto legend = header.removeFromRight (static_cast<float> (headerLegendWidth));
    g.setColour (kOutput);
    g.drawText ("OUT", legend.removeFromRight (30.0f).toNearestInt(),
                juce::Justification::centredRight, false);
    g.setColour (kInput);
    g.drawText ("IN", legend.removeFromRight (24.0f).toNearestInt(),
                juce::Justification::centredRight, false);

    inner.removeFromTop (2.0f);

    auto timeAxis = inner.removeFromBottom (16.0f);
    auto plot = inner;
    g.setColour (kPanel);
    g.fillRoundedRectangle (plot, 6.0f);

    const float centre = plot.getCentreY();
    const float scale = plot.getHeight() * 0.45f;

    const int dbMarks[] = { 0, -20, -40, -60 };

    g.setColour (kGrid);
    for (const int db : dbMarks)
    {
        const float y = centre
                        - ((static_cast<float> (db) + 60.0f) / 60.0f) * scale;
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    g.setColour (kDim);
    g.setFont (juce::Font (juce::FontOptions (polarity::type::annotation)));
    for (const int db : dbMarks)
    {
        const float y = centre
                        - ((static_cast<float> (db) + 60.0f) / 60.0f) * scale;
        g.drawText (juce::String (db),
                    juce::Rectangle<int> (0, juce::roundToInt (y) - 5, 24, 10),
                    juce::Justification::right, false);
    }

    g.setColour (kGrid);
    g.drawHorizontalLine (juce::roundToInt (plot.getBottom()),
                          plot.getX(), plot.getRight());

    g.setColour (kDim);
    juce::String leftTime;
    if (windowMs >= 1000)
        leftTime = juce::String (static_cast<double> (windowMs) / 1000.0, 1) + " s";
    else
        leftTime = juce::String (windowMs) + " ms";

    g.drawText ("-" + leftTime,
                juce::Rectangle<int> (juce::roundToInt (plot.getX()),
                                      juce::roundToInt (timeAxis.getY()) + 1,
                                      90, 12),
                juce::Justification::left, false);
    g.drawText ("0",
                juce::Rectangle<int> (juce::roundToInt (plot.getRight()) - 20,
                                      juce::roundToInt (timeAxis.getY()) + 1,
                                      20, 12),
                juce::Justification::right, false);

    const int points = PolarityAudioProcessor::getWaveformPointCount();
    const auto levelToNormalised = [] (float level)
    {
        const float db = 20.0f * std::log10 (std::max (level, 1.0e-6f));
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };

    const auto rmsValueAt = [this] (int point, bool input)
    {
        return input ? inputRmsAt (point) : outputRmsAt (point);
    };

    const auto smoothedRmsAt = [&rmsValueAt, points] (int point, bool input)
    {
        const int previous = juce::jlimit (0, points - 1, point - 1);
        const int next = juce::jlimit (0, points - 1, point + 1);
        return 0.25f * rmsValueAt (previous, input)
               + 0.5f * rmsValueAt (point, input)
               + 0.25f * rmsValueAt (next, input);
    };

    const auto envelopeValueAt = [&] (int point, bool input)
    {
        if (metric == Metric::Peak)
            return levelToNormalised (input ? inputPeakAt (point)
                                            : outputPeakAt (point));

        return levelToNormalised (smoothedRmsAt (point, input));
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

    for (int i = 0; i < points; ++i)
    {
        const float x = xAt (i);
        const float inputTop = centre - envelopeValueAt (i, true) * scale;
        const float outputTop = centre - envelopeValueAt (i, false) * scale;

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

    for (int i = points - 1; i >= 0; --i)
    {
        const float x = xAt (i);
        const float inputBottom = centre + envelopeValueAt (i, true) * scale;
        const float outputBottom = centre + envelopeValueAt (i, false) * scale;

        inputEnvelope.lineTo (x, inputBottom);
        outputEnvelope.lineTo (x, outputBottom);
    }

    inputEnvelope.closeSubPath();
    outputEnvelope.closeSubPath();

    g.setColour (kOutput.withAlpha (0.14f));
    g.fillPath (outputEnvelope);

    g.setColour (kInput.withAlpha (0.75f));
    g.strokePath (inputEnvelope, juce::PathStrokeType (1.0f));

    g.setColour (kOutput);
    g.strokePath (outputEnvelope,
                  juce::PathStrokeType (1.2f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
}
