#include "MeterPanel.h"

#include "../PluginProcessor.h"
#include "PolarityTheme.h"
#include "PolarityType.h"

#include <cmath>

MeterPanel::MeterPanel (PolarityAudioProcessor& processor)
    : processorRef (processor)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void MeterPanel::timerCallback()
{
    repaint();
}

void MeterPanel::paint (juce::Graphics& g)
{
    g.fillAll (polarity::theme::paper);

    auto area = getLocalBounds().toFloat().reduced (8.0f);

    juce::DropShadow shadow (juce::Colour (0x12000000), 6, { 0, 2 });
    juce::Path panelPath;
    panelPath.addRoundedRectangle (area, 8.0f);
    shadow.drawForPath (g, panelPath);

    g.setColour (polarity::theme::panel);
    g.fillRoundedRectangle (area, 8.0f);

    auto inner = area.reduced (6.0f);

    auto title = inner.removeFromTop (14.0f);
    g.setColour (polarity::theme::secondary);
    g.setFont (juce::Font (juce::FontOptions (polarity::type::annotation)));
    g.drawText ("M / S", title, juce::Justification::centred, false);

    auto corrArea = inner.removeFromBottom (30.0f);

    auto bars = inner;
    auto labelRow = bars.removeFromTop (11.0f);
    auto valueRow = bars.removeFromBottom (11.0f);
    const float halfWidth = bars.getWidth() * 0.5f;
    auto midColumn = bars.removeFromLeft (halfWidth);
    auto sideColumn = bars;
    const float barWidth = juce::jmin (22.0f, halfWidth * 0.58f);
    auto midBar = midColumn.withSizeKeepingCentre (barWidth, midColumn.getHeight());
    auto sideBar = sideColumn.withSizeKeepingCentre (barWidth, sideColumn.getHeight());

    drawBar (g, midBar, processorRef.getMeterMidDb(), polarity::theme::cobalt);
    drawBar (g, sideBar, processorRef.getMeterSideDb(), polarity::theme::violet);

    g.setFont (juce::Font (juce::FontOptions (polarity::type::annotation)));
    g.setColour (polarity::theme::secondary);

    auto midLabel = labelRow.removeFromLeft (halfWidth);
    g.drawText ("M", midLabel, juce::Justification::centred, false);
    g.drawText ("S", labelRow, juce::Justification::centred, false);

    g.setColour (polarity::theme::ink);
    auto midValue = valueRow.removeFromLeft (halfWidth);
    g.drawText (juce::String (juce::roundToInt (processorRef.getMeterMidDb())),
                midValue, juce::Justification::centred, false);
    g.drawText (juce::String (juce::roundToInt (processorRef.getMeterSideDb())),
                valueRow, juce::Justification::centred, false);

    const float correlation = processorRef.getMeterCorrelation();
    const juce::Colour correlationColour =
        correlation < -0.1f ? polarity::theme::vermilion
                            : (correlation < 0.1f ? polarity::theme::amber
                                                  : polarity::theme::teal);

    g.setColour (correlationColour);
    g.setFont (juce::Font (juce::FontOptions (polarity::type::annotation)));
    g.drawText ("CORR " + juce::String (correlation, 2),
                corrArea.removeFromTop (14.0f),
                juce::Justification::centred,
                false);

    auto correlationBar = corrArea.removeFromTop (5.0f).reduced (4.0f, 0.0f);
    g.setColour (polarity::theme::panelAlt);
    g.fillRect (correlationBar);

    const float centre = correlationBar.getCentreX();
    const float width = correlation * 0.5f * correlationBar.getWidth();
    const float x = width >= 0.0f ? centre : centre + width;

    g.setColour (correlationColour);
    g.fillRect (x, correlationBar.getY(), std::abs (width), correlationBar.getHeight());
}

void MeterPanel::drawBar (juce::Graphics& g,
                          juce::Rectangle<float> rect,
                          float db,
                          juce::Colour colour)
{
    g.setColour (polarity::theme::panelAlt);
    g.fillRoundedRectangle (rect, 2.0f);

    const float amount = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    auto fill = rect.withTop (rect.getBottom() - rect.getHeight() * amount);

    g.setColour (colour);
    g.fillRoundedRectangle (fill, 2.0f);
}
