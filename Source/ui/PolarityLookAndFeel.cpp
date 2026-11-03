#include "PolarityLookAndFeel.h"

#include "PolarityTheme.h"
#include "PolarityType.h"

namespace
{
using namespace polarity::theme;

juce::Font flatFont (float height)
{
    return juce::Font (juce::FontOptions (height));
}
} // namespace

PolarityLookAndFeel::PolarityLookAndFeel()
{
    setColour (juce::Label::textColourId, ink);

    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, ink);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, paper);

    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId, secondary);

    setColour (juce::PopupMenu::backgroundColourId, paperBright);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, panelAlt);
    setColour (juce::PopupMenu::highlightedTextColourId, ink);

    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::Slider::textBoxBackgroundColourId, paperBright);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour (juce::ToggleButton::textColourId, ink);
    setColour (juce::ToggleButton::tickColourId, ink);

    setColour (juce::ScrollBar::thumbColourId, gridStrong);
    setColour (juce::ScrollBar::trackColourId, panelAlt);
}

juce::Font PolarityLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return flatFont (polarity::type::control);
}

juce::Font PolarityLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return flatFont (polarity::type::control);
}

juce::Label* PolarityLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    if (label != nullptr)
        label->setFont (juce::Font (juce::FontOptions (polarity::type::control)));

    return label;
}

void PolarityLookAndFeel::drawButtonBackground (
    juce::Graphics& g,
    juce::Button& button,
    const juce::Colour& backgroundColour,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown)
{
    const bool on = button.getToggleState();
    auto fill = on ? button.findColour (juce::TextButton::buttonOnColourId)
                   : backgroundColour;

    if (! button.isEnabled())
        return;

    if (shouldDrawButtonAsDown)
        fill = fill.darker (0.08f);
    else if (shouldDrawButtonAsHighlighted && fill.getAlpha() > 0)
        fill = fill.brighter (0.03f);

    const auto bounds = button.getLocalBounds().toFloat();

    if (fill.getAlpha() > 0)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 4.0f);
    }
    else if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
    {
        g.setColour (paperBright.withAlpha (0.55f));
        g.fillRoundedRectangle (bounds, 4.0f);
    }
}

void PolarityLookAndFeel::drawButtonText (juce::Graphics& g,
                                          juce::TextButton& button,
                                          bool,
                                          bool)
{
    auto colour = button.getToggleState()
                      ? button.findColour (juce::TextButton::textColourOnId)
                      : button.findColour (juce::TextButton::textColourOffId);

    if (! button.isEnabled())
        colour = muted;

    g.setColour (colour);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().reduced (4),
                      juce::Justification::centred,
                      1);
}

void PolarityLookAndFeel::drawToggleButton (juce::Graphics& g,
                                            juce::ToggleButton& button,
                                            bool,
                                            bool)
{
    auto bounds = button.getLocalBounds().reduced (0, 2);
    auto box = bounds.removeFromLeft (bounds.getHeight()).reduced (2);

    const bool on = button.getToggleState();

    g.setColour (on ? ink : panelAlt);
    g.fillRoundedRectangle (box.toFloat(), 3.0f);

    if (on)
    {
        g.setColour (paper);
        g.fillRoundedRectangle (box.reduced (3).toFloat(), 2.0f);
    }

    g.setColour (button.isEnabled() ? ink : muted);
    g.setFont (flatFont (polarity::type::control));
    g.drawText (button.getButtonText(), bounds.reduced (6, 0),
                juce::Justification::centredLeft, false);
}

void PolarityLookAndFeel::drawComboBox (juce::Graphics& g,
                                        int width,
                                        int height,
                                        bool,
                                        int,
                                        int,
                                        int,
                                        int,
                                        juce::ComboBox& box)
{
    const juce::Rectangle<float> bounds (0.0f, 0.0f,
                                         static_cast<float> (width),
                                         static_cast<float> (height));

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 4.0f);

    juce::Path arrow;
    const float centreX = static_cast<float> (width) - 13.0f;
    const float centreY = static_cast<float> (height) * 0.5f;
    arrow.addTriangle (centreX - 4.0f, centreY - 2.0f,
                       centreX + 4.0f, centreY - 2.0f,
                       centreX, centreY + 3.0f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void PolarityLookAndFeel::drawLinearSlider (
    juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPos, float, float,
    const juce::Slider::SliderStyle,
    juce::Slider&)
{
    const auto area = juce::Rectangle<float> (static_cast<float> (x),
                                              static_cast<float> (y),
                                              static_cast<float> (width),
                                              static_cast<float> (height));
    const float centreY = area.getCentreY();
    const float trackY = centreY - 1.0f;

    g.setColour (gridStrong);
    g.fillRect (area.getX(), trackY, area.getWidth(), 2.0f);

    const float clamped = juce::jlimit (area.getX(), area.getRight(), sliderPos);

    g.setColour (teal);
    g.fillRect (area.getX(), trackY, clamped - area.getX(), 2.0f);

    const juce::Rectangle<float> thumb (clamped - 3.5f, centreY - 8.0f,
                                        7.0f, 16.0f);
    g.setColour (ink);
    g.fillRoundedRectangle (thumb, 2.0f);
}

void PolarityLookAndFeel::drawPopupMenuBackground (juce::Graphics& g,
                                                   int,
                                                   int)
{
    g.fillAll (paperBright);
}

void PolarityLookAndFeel::drawTooltip (juce::Graphics& g,
                                       const juce::String& tooltipText,
                                       int width,
                                       int height)
{
    const juce::Rectangle<float> bounds (0.0f, 0.0f,
                                         static_cast<float> (width),
                                         static_cast<float> (height));

    g.setColour (paperBright);
    g.fillRoundedRectangle (bounds.reduced (0.5f), 6.0f);

    g.setColour (border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    g.setColour (ink);
    g.setFont (flatFont (polarity::type::control));
    g.drawFittedText (tooltipText, bounds.reduced (8.0f, 4.0f).toNearestInt(),
                      juce::Justification::centredLeft, 3);
}
