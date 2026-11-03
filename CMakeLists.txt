cmake_minimum_required(VERSION 3.22)

project(Polarity VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE 8.0.15, vendored locally in ThirdParty/ (downloaded release zip).
add_subdirectory(ThirdParty/JUCE-8.0.15/JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(Polarity
    PRODUCT_NAME            "Polarity"
    COMPANY_NAME            "Polarity Audio"
    BUNDLE_ID               "com.polarityaudio.polarity"
    PLUGIN_MANUFACTURER_CODE "PlyA"
    PLUGIN_CODE             "Ply1"
    FORMATS                 VST3 Standalone
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES         Fx EQ
)

target_sources(Polarity PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/dsp/CurveModel.cpp
    Source/dsp/CurveModel.h
    Source/dsp/Fft.cpp
    Source/dsp/Fft.h
    Source/dsp/FirDesigner.cpp
    Source/dsp/FirDesigner.h
    Source/dsp/InversionEngine.cpp
    Source/dsp/InversionEngine.h
    Source/ui/CurveEditor.cpp
    Source/ui/CurveEditor.h
    Source/ui/PolarityLookAndFeel.cpp
    Source/ui/PolarityLookAndFeel.h
    Source/ui/PolarityTheme.h
    Source/ui/MeterPanel.cpp
    Source/ui/MeterPanel.h
    Source/ui/WaveformPanel.cpp
    Source/ui/WaveformPanel.h
    Source/state/PresetManager.cpp
    Source/state/PresetManager.h
)

target_compile_definitions(Polarity PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Polarity
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline integration test: drives the real AudioProcessor without a DAW.
juce_add_console_app(PolarityTests PRODUCT_NAME "PolarityTests")

target_sources(PolarityTests PRIVATE
    Tests/processor_tests.cpp
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/dsp/CurveModel.cpp
    Source/dsp/CurveModel.h
    Source/dsp/Fft.cpp
    Source/dsp/Fft.h
    Source/dsp/FirDesigner.cpp
    Source/dsp/FirDesigner.h
    Source/dsp/InversionEngine.cpp
    Source/dsp/InversionEngine.h
    Source/ui/CurveEditor.cpp
    Source/ui/CurveEditor.h
    Source/ui/PolarityLookAndFeel.cpp
    Source/ui/PolarityLookAndFeel.h
    Source/ui/PolarityTheme.h
    Source/ui/MeterPanel.cpp
    Source/ui/MeterPanel.h
    Source/ui/WaveformPanel.cpp
    Source/ui/WaveformPanel.h
    Source/state/PresetManager.cpp
    Source/state/PresetManager.h
)

target_include_directories(PolarityTests PRIVATE Source)

target_compile_definitions(PolarityTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    "JucePlugin_Name=\"Polarity\""
)

target_link_libraries(PolarityTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
