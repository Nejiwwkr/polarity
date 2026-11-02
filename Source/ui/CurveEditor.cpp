#include "CurveEditor.h"

#include "../PluginProcessor.h"
#include "PolarityTheme.h"
#include "PolarityType.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMinFreq = 20.0;
constexpr double kMaxFreq = 20000.0;
constexpr double kMaxAmount = 2.0;

const juce::Colour kBackground = polarity::theme::paper;
const juce::Colour kPlot       = polarity::theme::paperBright;
const juce::Colour kGrid       = polarity::theme::grid;
const juce::Colour kNullLine   = polarity::theme::gridStrong;
const juce::Colour kTextDim    = polarity::theme::secondary;
const juce::Colour kText       = polarity::theme::ink;
const juce::Colour kLeft       = polarity::theme::cobalt;
const juce::Colour kRight      = polarity::theme::vermilion;
const juce::Colour kStereo     = polarity::theme::violet;
const juce::Colour kWarning    = polarity::theme::amber;
const juce::Colour kSpecInput  = polarity::theme::secondary.withAlpha (0.70f);
const juce::Colour kSpecOutput = polarity::theme::teal;
} // namespace

CurveEditor::CurveEditor (PolarityAudioProcessor& processor)
    : processorRef (processor)
{
    setTool (Tool::Draw);
    setLayer (Layer::Stereo);
    processorRef.getCurveBroadcaster().addChangeListener (this);
    reloadFromProcessor();
    startTimerHz (30);
}

CurveEditor::~CurveEditor()
{
    processorRef.getCurveBroadcaster().removeChangeListener (this);
}

void CurveEditor::setLayer (Layer newLayer)
{
    if (layer == newLayer)
        return;

    layer = newLayer;
    repaint();
}

void CurveEditor::setTool (Tool newTool)
{
    tool = newTool;

    switch (tool)
    {
        case Tool::Node:
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            break;

        case Tool::Draw:
        case Tool::Erase:
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
            break;
    }
}

void CurveEditor::setSpectrumVisible (bool visible)
{
    if (spectrumVisible == visible)
        return;

    spectrumVisible = visible;
    repaint();
}

void CurveEditor::setPolarityVisible (bool visible)
{
    if (polarityVisible == visible)
        return;

    polarityVisible = visible;
    repaint();
}

void CurveEditor::setSpectrumSlope (float dBPerOctave)
{
    const float clamped = juce::jlimit (0.0f, 12.0f, dBPerOctave);

    if (spectrumSlope == clamped)
        return;

    spectrumSlope = clamped;
    repaint();
}

void CurveEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    reloadFromProcessor();
}

void CurveEditor::timerCallback()
{
    repaint();
}

void CurveEditor::reloadFromProcessor()
{
    leftPoints = processorRef.getCurvePoints (0);
    rightPoints = processorRef.getCurvePoints (1);
    undoStack.clear();
    redoStack.clear();
    repaint();

    if (onEdit != nullptr)
        onEdit();
}

std::vector<polarity::dsp::CurvePoint>& CurveEditor::editablePoints()
{
    return layer == Layer::Right ? rightPoints : leftPoints;
}

const std::vector<polarity::dsp::CurvePoint>& CurveEditor::displayedPoints() const
{
    return layer == Layer::Right ? rightPoints : leftPoints;
}

void CurveEditor::applyEdits()
{
    switch (layer)
    {
        case Layer::Left:
            processorRef.setCurvePoints (0, leftPoints);
            break;

        case Layer::Right:
            processorRef.setCurvePoints (1, rightPoints);
            break;

        case Layer::Stereo:
            rightPoints = leftPoints;
            processorRef.setCurves (leftPoints, rightPoints);
            break;
    }

    repaint();

    if (onEdit != nullptr)
        onEdit();
}

void CurveEditor::pushUndo()
{
    undoStack.push_back ({ leftPoints, rightPoints });

    if (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
}

void CurveEditor::undo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back ({ leftPoints, rightPoints });

    auto snapshot = std::move (undoStack.back());
    undoStack.pop_back();

    leftPoints = std::move (snapshot.left);
    rightPoints = std::move (snapshot.right);
    processorRef.setCurves (leftPoints, rightPoints);
    repaint();

    if (onEdit != nullptr)
        onEdit();
}

void CurveEditor::redo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back ({ leftPoints, rightPoints });

    auto snapshot = std::move (redoStack.back());
    redoStack.pop_back();

    leftPoints = std::move (snapshot.left);
    rightPoints = std::move (snapshot.right);
    processorRef.setCurves (leftPoints, rightPoints);
    repaint();

    if (onEdit != nullptr)
        onEdit();
}

juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds().toFloat()
        .reduced (4.0f)
        .withTrimmedTop (10.0f)
        .withTrimmedLeft (24.0f)
        .withTrimmedRight (32.0f)
        .withTrimmedBottom (18.0f);
}

float CurveEditor::freqToX (double freq) const
{
    const auto area = plotArea();
    const double t = std::log10 (juce::jlimit (kMinFreq, kMaxFreq, freq) / kMinFreq)
                     / std::log10 (kMaxFreq / kMinFreq);
    return area.getX() + static_cast<float> (t) * area.getWidth();
}

double CurveEditor::xToFreq (float x) const
{
    const auto area = plotArea();
    const double t = juce::jlimit (0.0, 1.0,
                                   static_cast<double> ((x - area.getX())
                                                        / area.getWidth()));
    return kMinFreq * std::pow (kMaxFreq / kMinFreq, t);
}

float CurveEditor::amountToY (double amount) const
{
    const auto area = plotArea();
    const double t = juce::jlimit (0.0, kMaxAmount, amount) / kMaxAmount;
    return area.getBottom() - static_cast<float> (t) * area.getHeight();
}

double CurveEditor::yToAmount (float y) const
{
    const auto area = plotArea();
    const double t = juce::jlimit (0.0, 1.0,
                                   static_cast<double> ((area.getBottom() - y)
                                                        / area.getHeight()));
    return t * kMaxAmount;
}

float CurveEditor::dbToY (float db, juce::Rectangle<float> area)
{
    const float t = juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 100.0f);
    return area.getBottom() - t * area.getHeight();
}

juce::Colour CurveEditor::amountToColour (double amount)
{
    const juce::Colour zero     { 0xffdce6f0 };
    const juce::Colour half     { 0xff5b8def };
    const juce::Colour null     { 0xffe4d6b8 };
    const juce::Colour inverted { 0xfff29e4c };
    const juce::Colour full     { 0xffd64627 };

    const double a = juce::jlimit (0.0, 2.0, amount);

    if (a <= 0.5)
        return zero.interpolatedWith (half, static_cast<float> (a / 0.5));

    if (a <= 1.0)
        return half.interpolatedWith (null, static_cast<float> ((a - 0.5) / 0.5));

    if (a <= 1.5)
        return null.interpolatedWith (inverted, static_cast<float> ((a - 1.0) / 0.5));

    return inverted.interpolatedWith (full, static_cast<float> ((a - 1.5) / 0.5));
}

const std::vector<polarity::dsp::CurvePoint>& CurveEditor::heatmapPoints() const
{
    return layer == Layer::Right ? rightPoints : leftPoints;
}

float CurveEditor::spectrumValueAt (float x,
                                    juce::Rectangle<float> area,
                                    bool output) const
{
    const int bands = PolarityAudioProcessor::getSpectrumBandCount();
    const float t = juce::jlimit (0.0f, 1.0f,
                                  (x - area.getX()) / area.getWidth());
    const float position = t * static_cast<float> (bands - 1);
    const int lower = static_cast<int> (std::floor (position));
    const int upper = std::min (lower + 1, bands - 1);
    const float frac = position - static_cast<float> (lower);

    const float lowerValue = output ? processorRef.getSpectrumOutput (lower)
                                    : processorRef.getSpectrumInput (lower);
    const float upperValue = output ? processorRef.getSpectrumOutput (upper)
                                    : processorRef.getSpectrumInput (upper);

    const double frequency = xToFreq (x);
    const float tilt = spectrumSlope
                       * static_cast<float> (std::log2 (frequency / 1000.0));

    return lowerValue + (upperValue - lowerValue) * frac + tilt;
}

void CurveEditor::drawSpectrumFill (juce::Graphics& g,
                                    juce::Rectangle<float> area) const
{
    if (spectrumVisible)
    {
        const polarity::dsp::CurveModel heatModel (heatmapPoints());

        for (float x = area.getX(); x < area.getRight(); x += 1.0f)
        {
            const float db = spectrumValueAt (x, area, true);
            const float y = juce::jlimit (area.getY(), area.getBottom(),
                                          dbToY (db, area));

            if (y >= area.getBottom() - 0.5f)
                continue;

            juce::Colour colour = polarity::theme::cobalt.withAlpha (0.08f);

            if (polarityVisible)
            {
                const double freq = xToFreq (x);
                colour = amountToColour (heatModel.amountAt (freq)).withAlpha (0.32f);
            }

            g.setColour (colour);
            g.fillRect (x, y, 1.0f, area.getBottom() - y);
        }
    }

    drawPolarityStrip (g, area);
}

void CurveEditor::drawPolarityStrip (juce::Graphics& g,
                                     juce::Rectangle<float> area) const
{
    if (! polarityVisible)
        return;

    auto strip = area.removeFromBottom (8.0f);
    const float rowHeight = strip.getHeight() * 0.5f;
    const polarity::dsp::CurveModel leftModel (leftPoints);
    const polarity::dsp::CurveModel rightModel (rightPoints);

    for (float x = strip.getX(); x < strip.getRight(); x += 1.0f)
    {
        const double freq = xToFreq (x);

        g.setColour (amountToColour (leftModel.amountAt (freq)).withAlpha (0.85f));
        g.fillRect (x, strip.getY(), 1.0f, rowHeight);

        g.setColour (amountToColour (rightModel.amountAt (freq)).withAlpha (0.85f));
        g.fillRect (x, strip.getY() + rowHeight, 1.0f,
                    strip.getHeight() - rowHeight);
    }
}

void CurveEditor::drawSpectrumLines (juce::Graphics& g,
                                     juce::Rectangle<float> area) const
{
    if (! spectrumVisible)
        return;

    juce::Path inputPath;
    juce::Path outputPath;
    bool first = true;

    for (float x = area.getX(); x <= area.getRight(); x += 1.0f)
    {
        const float inputY = juce::jlimit (
            area.getY(), area.getBottom(),
            dbToY (spectrumValueAt (x, area, false), area));
        const float outputY = juce::jlimit (
            area.getY(), area.getBottom(),
            dbToY (spectrumValueAt (x, area, true), area));

        if (first)
        {
            inputPath.startNewSubPath (x, inputY);
            outputPath.startNewSubPath (x, outputY);
            first = false;
        }
        else
        {
            inputPath.lineTo (x, inputY);
            outputPath.lineTo (x, outputY);
        }
    }

    g.setColour (kSpecInput);
    g.strokePath (inputPath, juce::PathStrokeType (1.0f));

    g.setColour (kSpecOutput);
    g.strokePath (outputPath,
                  juce::PathStrokeType (1.6f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
}

void CurveEditor::drawDbScale (juce::Graphics& g,
                               juce::Rectangle<float> area) const
{
    if (! spectrumVisible)
        return;

    const float x = area.getRight();
    g.setColour (kGrid);
    g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

    g.setFont (juce::Font (juce::FontOptions (polarity::type::annotation)));

    for (int db = 0; db >= -100; db -= 20)
    {
        const float y = dbToY (static_cast<float> (db), area);

        g.setColour (kTextDim);
        g.drawHorizontalLine (juce::roundToInt (y), x, x + 4.0f);
        g.drawText (juce::String (db),
                    juce::Rectangle<int> (juce::roundToInt (x) + 5,
                                          juce::roundToInt (y) - 5, 24, 10),
                    juce::Justification::left,
                    false);
    }

    g.setColour (kTextDim.withAlpha (0.8f));
    g.drawText ("dBFS",
                juce::Rectangle<int> (juce::roundToInt (x) - 42,
                                      juce::roundToInt (area.getY()) + 2, 38, 10),
                juce::Justification::right,
                false);
}

void CurveEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    const auto area = plotArea();
    juce::DropShadow shadow (juce::Colour (0x14000000), 10, { 0, 3 });
    juce::Path panelPath;
    panelPath.addRoundedRectangle (area, 8.0f);
    shadow.drawForPath (g, panelPath);

    g.setColour (kPlot);
    g.fillRoundedRectangle (area, 8.0f);

    drawSpectrumFill (g, area);

    g.setFont (juce::Font (juce::FontOptions (polarity::type::annotation)));

    const double freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };

    for (const double freq : freqs)
    {
        const float x = freqToX (freq);

        g.setColour (kGrid);
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

        juce::String label;

        if (freq >= 1000.0)
            label = juce::String (freq / 1000.0, 0) + "k";
        else
            label = juce::String (freq, 0);

        g.setColour (kTextDim);
        g.drawText (label,
                    juce::Rectangle<int> (juce::roundToInt (x) - 20,
                                          juce::roundToInt (area.getBottom()) + 1,
                                          40, 14),
                    juce::Justification::centred,
                    false);
    }

    const double amounts[] = { 0.0, 0.5, 1.0, 1.5, 2.0 };
    const char* amountLabels[] = { "0", "0.5", "NULL", "1.5", "INV" };

    for (int i = 0; i < 5; ++i)
    {
        const float y = amountToY (amounts[i]);

        g.setColour (amounts[i] == 1.0 ? kNullLine : kGrid);
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());

        g.setColour (amounts[i] == 1.0 ? kText : kTextDim);
        g.drawText (amountLabels[i],
                    juce::Rectangle<int> (0, juce::roundToInt (y) - 7,
                                          juce::roundToInt (area.getX()) - 4, 14),
                    juce::Justification::centredRight,
                    false);
    }

    drawSpectrumLines (g, area);
    drawDbScale (g, area);

    if (layer == Layer::Stereo)
    {
        paintCurve (g, leftPoints, kStereo.withAlpha (0.35f), true, false);
        paintCurve (g, rightPoints, kStereo.withAlpha (0.35f), true, false);
    }
    else if (layer == Layer::Left)
    {
        paintCurve (g, rightPoints, kRight, false, false);
    }
    else
    {
        paintCurve (g, leftPoints, kLeft, false, false);
    }

    if (layer != Layer::Right)
        paintCurve (g, leftPoints, kLeft, true, true);

    if (layer != Layer::Left)
        paintCurve (g, rightPoints, kRight, true, true);

    if (mouseInside && area.contains (mousePosition))
    {
        const auto& points = displayedPoints();
        const polarity::dsp::CurveModel model (points);
        const double freq = xToFreq (mousePosition.x);
        const double amount = model.amountAt (freq);
        const double gain = 1.0 - amount;

        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawVerticalLine (juce::roundToInt (mousePosition.x),
                            area.getY(), area.getBottom());

        const float dotY = amountToY (amount);
        g.setColour (kText);
        g.fillEllipse (mousePosition.x - 3.0f, dotY - 3.0f, 6.0f, 6.0f);

        juce::String text;
        text << juce::String (freq, freq < 100.0 ? 1 : 0) << " Hz   A "
             << juce::String (amount, 2);

        if (std::abs (gain) < 1.0e-4)
        {
            text << "   NULL";
        }
        else
        {
            const double db = 20.0 * std::log10 (std::abs (gain));
            text << "   " << (db >= 0.0 ? "+" : "") << juce::String (db, 1) << " dB";

            if (gain < 0.0)
                text << "  INV";
        }

        g.setColour (kText);
        g.setFont (juce::Font (juce::FontOptions (polarity::type::primary)));
        g.drawText (text,
                    area.reduced (8.0f).removeFromTop (18.0f).toNearestInt(),
                    juce::Justification::left,
                    false);
    }

    if (curvesDiffer())
    {
        g.setColour (kWarning);
        g.setFont (juce::Font (juce::FontOptions (polarity::type::annotation)));
        g.drawText ("L/R differ - check mono",
                    area.reduced (8.0f).removeFromTop (18.0f).toNearestInt(),
                    juce::Justification::right,
                    false);
    }

}

void CurveEditor::paintCurve (juce::Graphics& g,
                              const std::vector<polarity::dsp::CurvePoint>& points,
                              juce::Colour colour,
                              bool active,
                              bool showPoints)
{
    if (points.size() < 2)
        return;

    const auto area = plotArea();
    const polarity::dsp::CurveModel model (points);

    juce::Path path;
    const int steps = juce::jmax (2, juce::roundToInt (area.getWidth()));

    for (int i = 0; i <= steps; ++i)
    {
        const float x = area.getX() + area.getWidth()
                        * static_cast<float> (i) / static_cast<float> (steps);
        const float y = amountToY (model.amountAt (xToFreq (x)));

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    g.setColour (active ? colour : colour.withAlpha (0.22f));
    g.strokePath (path,
                  juce::PathStrokeType (active ? 2.2f : 1.2f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    if (! active || ! showPoints)
        return;

    for (std::size_t i = 1; i + 1 < points.size(); ++i)
    {
        const float x = freqToX (points[i].freqHz);
        const float y = amountToY (points[i].amount);

        g.setColour (colour);
        g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        g.setColour (polarity::theme::paperBright.withAlpha (0.90f));
        g.drawEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f, 1.0f);
    }
}

bool CurveEditor::curvesDiffer() const
{
    const polarity::dsp::CurveModel left (leftPoints);
    const polarity::dsp::CurveModel right (rightPoints);

    for (int i = 0; i <= 32; ++i)
    {
        const double freq = kMinFreq * std::pow (kMaxFreq / kMinFreq, i / 32.0);

        if (std::abs (left.amountAt (freq) - right.amountAt (freq)) > 0.15)
            return true;
    }

    return false;
}

int CurveEditor::findPointNear (
    const std::vector<polarity::dsp::CurvePoint>& points,
    juce::Point<float> position,
    float radius) const
{
    int best = -1;
    float bestDistance = radius;

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const juce::Point<float> point (freqToX (points[i].freqHz),
                                        amountToY (points[i].amount));
        const float distance = point.getDistanceFrom (position);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int> (i);
        }
    }

    return best;
}

void CurveEditor::addOrUpdatePoint (double freq, double amount)
{
    auto& points = editablePoints();

    const double clampedFreq = juce::jlimit (kMinFreq + 0.5, kMaxFreq - 0.5, freq);
    const double clampedAmount = juce::jlimit (0.0, kMaxAmount, amount);
    const double tolerance = juce::jmax (0.5, clampedFreq * 0.02);

    int nearest = -1;
    double bestDistance = tolerance;

    for (std::size_t i = 1; i + 1 < points.size(); ++i)
    {
        const double distance = std::abs (points[i].freqHz - clampedFreq);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            nearest = static_cast<int> (i);
        }
    }

    if (nearest >= 0)
    {
        points[static_cast<std::size_t> (nearest)].amount = clampedAmount;
        return;
    }

    points.push_back ({ clampedFreq, clampedAmount });
    std::sort (points.begin(), points.end(),
               [] (const auto& a, const auto& b)
               {
                   return a.freqHz < b.freqHz;
               });
}

void CurveEditor::eraseNear (float x)
{
    auto& points = editablePoints();
    const double freq = xToFreq (x);
    const double radius = freq * 0.05;

    for (int i = static_cast<int> (points.size()) - 2; i >= 1; --i)
        if (std::abs (points[static_cast<std::size_t> (i)].freqHz - freq) <= radius)
            points.erase (points.begin() + i);

    addOrUpdatePoint (freq, 0.0);
}

void CurveEditor::strokeTo (juce::Point<float> position)
{
    const auto delta = position - lastDragPosition;
    const int steps = juce::jmax (1, juce::roundToInt (std::abs (delta.x) / 2.0f));

    for (int i = 1; i <= steps; ++i)
    {
        const auto point = lastDragPosition
                           + delta * (static_cast<float> (i) / static_cast<float> (steps));
        addOrUpdatePoint (xToFreq (point.x), yToAmount (point.y));
    }

    lastDragPosition = position;
    applyEdits();
}

void CurveEditor::mouseDown (const juce::MouseEvent& event)
{
    mousePosition = event.position;
    mouseInside = true;

    const auto area = plotArea();

    if (! area.contains (event.position))
    {
        repaint();
        return;
    }

    if (event.mods.isPopupMenu())
    {
        auto& points = editablePoints();
        const int index = findPointNear (points, event.position);

        if (index > 0 && index < static_cast<int> (points.size()) - 1)
        {
            pushUndo();
            points.erase (points.begin() + index);
            applyEdits();
        }

        return;
    }

    if (tool == Tool::Node)
    {
        auto& points = editablePoints();
        const int index = findPointNear (points, event.position);

        if (index > 0 && index < static_cast<int> (points.size()) - 1)
        {
            pushUndo();
            dragPointIndex = index;
            dragging = true;
        }

        return;
    }

    pushUndo();
    dragging = true;
    lastDragPosition = event.position;

    if (tool == Tool::Draw)
        addOrUpdatePoint (xToFreq (event.position.x), yToAmount (event.position.y));
    else
        eraseNear (event.position.x);

    applyEdits();
}

void CurveEditor::mouseDrag (const juce::MouseEvent& event)
{
    mousePosition = event.position;
    mouseInside = true;

    if (! dragging)
    {
        repaint();
        return;
    }

    if (tool == Tool::Node && dragPointIndex >= 0)
    {
        auto& points = editablePoints();

        if (dragPointIndex < static_cast<int> (points.size()))
        {
            const auto index = static_cast<std::size_t> (dragPointIndex);
            const double minFreq = index > 0
                                       ? points[index - 1].freqHz * 1.001
                                       : kMinFreq + 0.5;
            const double maxFreq = index + 1 < points.size()
                                       ? points[index + 1].freqHz * 0.999
                                       : kMaxFreq - 0.5;

            points[index].freqHz = juce::jlimit (minFreq, maxFreq,
                                                 xToFreq (event.position.x));
            points[index].amount = juce::jlimit (0.0, kMaxAmount,
                                                 yToAmount (event.position.y));
            applyEdits();
        }
    }
    else if (tool == Tool::Draw)
    {
        strokeTo (event.position);
    }
    else if (tool == Tool::Erase)
    {
        eraseNear (event.position.x);
        applyEdits();
    }

    repaint();
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    dragPointIndex = -1;
    repaint();
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (tool != Tool::Node || ! plotArea().contains (event.position))
        return;

    auto& points = editablePoints();

    if (findPointNear (points, event.position) >= 0)
        return;

    pushUndo();
    addOrUpdatePoint (xToFreq (event.position.x), yToAmount (event.position.y));
    applyEdits();
}

void CurveEditor::mouseMove (const juce::MouseEvent& event)
{
    mousePosition = event.position;
    mouseInside = true;
}

void CurveEditor::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    repaint();
}

void CurveEditor::resized()
{
}
