#include "PresetManager.h"

#include "../PluginProcessor.h"

#include <utility>

PresetManager::PresetManager (PolarityAudioProcessor& processorIn, juce::File directoryIn)
    : processor (processorIn),
      directory (std::move (directoryIn))
{
    if (directory.getFullPathName().isEmpty())
        directory = juce::File::getSpecialLocation (
                        juce::File::userApplicationDataDirectory)
                        .getChildFile ("Polarity")
                        .getChildFile ("Presets");
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;

    if (! directory.isDirectory())
        return names;

    for (const auto& entry : directory.findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (entry.getFileNameWithoutExtension());

    names.sort (true);
    return names;
}

juce::String PresetManager::sanitiseName (const juce::String& nameIn)
{
    auto name = juce::File::createLegalFileName (nameIn.trim());

    if (name.endsWithIgnoreCase (".xml"))
        name = name.dropLastCharacters (4);

    name = name.replace ("..", "_").trim();

    if (name.isEmpty() || name == "." || name == "_")
        name = "Preset";

    return name;
}

bool PresetManager::savePreset (const juce::String& nameIn)
{
    const auto name = sanitiseName (nameIn);

    if (name.isEmpty())
        return false;

    if (! directory.isDirectory() && directory.createDirectory().failed())
        return false;

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    return directory.getChildFile (name + ".xml")
        .replaceWithData (state.getData(), state.getSize());
}

bool PresetManager::loadPreset (const juce::String& nameIn)
{
    const auto name = sanitiseName (nameIn);
    const auto file = directory.getChildFile (name + ".xml");

    if (! file.existsAsFile())
        return false;

    juce::MemoryBlock state;

    if (! file.loadFileAsData (state))
        return false;

    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    return true;
}
