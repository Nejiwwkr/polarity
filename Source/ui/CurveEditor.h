#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../dsp/CurveModel.h"

#include <functional>
#include <vector>

class PolarityAudioProcessor;

/**
    The frequency / inversion-amount canvas.

    Layer semantics:
      Left   -> edits A_L only
      Right  -> edits A_R only
      Stereo -> edits both (writes the same points to L and R)

    Draw/Erase write a point at the cursor frequency; Node drags existing
    control points.  The processor rebuilds the FIR kernels synchronously on
    the message thread and the audio engine crossfades to them.
*/
class CurveEditor final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    enum class Layer { Left, Stereo, Right };
    enum class Tool { Draw, Node, Erase };

    explicit CurveEditor (PolarityAudioProcessor&);
    ~CurveEditor() override;

    void setLayer (Layer newLayer);
    void setTool (Tool newTool);
    void setSpectrumVisible (bool visible);
    void setPolarityVisible (bool visible);
    void setSpectrumSlope (float dBPerOctave);

    /** Distance from the component's right edge to the drawn plot's right
        edge (4px outer inset + 32px dBFS label gutter). */
    static constexpr int plotRightInset = 4 + 32;

    Layer getLayer() const noexcept { return layer; }
    Tool getTool() const noexcept { return tool; }
    bool isSpectrumVisible() const noexcept { return spectrumVisible; }
    bool isPolarityVisible() const noexcept { return polarityVisible; }
    float getSpectrumSlope() const noexcept { return spectrumSlope; }

    void undo();
    void redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    std::function<void()> onEdit;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct Snapshot
    {
        std::vector<polarity::dsp::CurvePoint> left;
        std::vector<polarity::dsp::CurvePoint> right;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void reloadFromProcessor();
    void applyEdits();
    void pushUndo();
    void addOrUpdatePoint (double freq, double amount);
    void eraseNear (float x);
    void strokeTo (juce::Point<float> position);
    int findPointNear (const std::vector<polarity::dsp::CurvePoint>& points,
                       juce::Point<float> position,
                       float radius = 8.0f) const;

    std::vector<polarity::dsp::CurvePoint>& editablePoints();
    const std::vector<polarity::dsp::CurvePoint>& displayedPoints() const;

    juce::Rectangle<float> plotArea() const;
    float freqToX (double freq) const;
    double xToFreq (float x) const;
    float amountToY (double amount) const;
    double yToAmount (float y) const;

    void paintCurve (juce::Graphics&,
                     const std::vector<polarity::dsp::CurvePoint>&,
                     juce::Colour,
                     bool active,
                     bool showPoints);
    bool curvesDiffer() const;

    void drawSpectrumFill (juce::Graphics&, juce::Rectangle<float>) const;
    void drawSpectrumLines (juce::Graphics&, juce::Rectangle<float>) const;
    void drawDbScale (juce::Graphics&, juce::Rectangle<float>) const;
    void drawPolarityStrip (juce::Graphics&, juce::Rectangle<float>) const;
    float spectrumValueAt (float x, juce::Rectangle<float> area, bool output) const;
    const std::vector<polarity::dsp::CurvePoint>& heatmapPoints() const;
    static juce::Colour amountToColour (double amount);
    static float dbToY (float db, juce::Rectangle<float> area);

    PolarityAudioProcessor& processorRef;

    std::vector<polarity::dsp::CurvePoint> leftPoints;
    std::vector<polarity::dsp::CurvePoint> rightPoints;

    Layer layer = Layer::Stereo;
    Tool tool = Tool::Draw;
    bool spectrumVisible = true;
    bool polarityVisible = true;
    float spectrumSlope = 4.5f;

    int dragPointIndex = -1;
    bool dragging = false;
    juce::Point<float> lastDragPosition;
    juce::Point<float> mousePosition;
    bool mouseInside = false;

    std::vector<Snapshot> undoStack;
    std::vector<Snapshot> redoStack;
    static constexpr std::size_t maxUndo = 64;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
};
