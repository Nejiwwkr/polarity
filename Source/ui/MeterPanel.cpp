#include "MeterPanel.h"

#include "../PluginProcessor.h"

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
    g.fillAll (juce::Colour (0xff111318));

    auto area = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (juce::Colour (0xff171a20));
    g.fillRoundedRectangle (area, 5.0f);

    auto inner = area.reduced (5.0f);

    auto title = inner.removeFromTop (14.0f);
    g.setColour (juce::Colour (0xff7a818c));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("M / S", title, juce::Justification::centred, false);

    auto corrArea = inner.removeFromBottom (30.0f);

    auto bars = inner;
    auto midBar = bars.removeFromLeft (bars.getWidth() * 0.5f).reduced (3.0f);
    auto sideBar = bars.reduced (3.0f);

    drawBar (g, midBar, processorRef.getMeterMidDb(),
             juce::Colour (0xff4da3ff), "M");
    drawBar (g, sideBar, processorRef.getMeterSideDb(),
             juce::Colour (0xffb58cff), "S");

    const float correlation = processorRef.getMeterCorrelation();
    const juce::Colour correlationColour =
        correlation < -0.1f ? juce::Colour (0xffff6b6b)
                            : (correlation < 0.1f ? juce::Colour (0xffffcc66)
                                                  : juce::Colour (0xff6fd08c));

    g.setColour (correlationColour);
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("CORR " + juce::String (correlation, 2),
                corrArea.removeFromTop (14.0f),
                juce::Justification::centred,
                false);

    auto correlationBar = corrArea.removeFromTop (5.0f).reduced (4.0f, 0.0f);
    g.setColour (juce::Colour (0xff252a33));
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
                          juce::Colour colour,
                          const juce::String& label)
{
    g.setColour (juce::Colour (0xff20252d));
    g.fillRoundedRectangle (rect, 3.0f);

    const float amount = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    auto fill = rect.withTop (rect.getBottom() - rect.getHeight() * amount);

    g.setColour (colour);
    g.fillRoundedRectangle (fill, 3.0f);

    g.setColour (juce::Colour (0xffd7dde6));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText (label, rect.removeFromTop (12.0f),
                juce::Justification::centred, false);
    g.drawText (juce::String (juce::roundToInt (db)),
                rect.removeFromBottom (12.0f),
                juce::Justification::centred, false);
}
