#pragma once

#include <juce_core/juce_core.h>

class PolarityAudioProcessor;

/** Minimal file-based preset bank: XML state blobs in a user folder. */
class PresetManager
{
public:
    explicit PresetManager (PolarityAudioProcessor&, juce::File directory = {});

    juce::StringArray getPresetNames() const;
    juce::File getDirectory() const { return directory; }

    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::String& name);

    static juce::String sanitiseName (const juce::String& name);

private:
    PolarityAudioProcessor& processor;
    juce::File directory;
};
