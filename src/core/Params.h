#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace cortex::params
{

// Parameter IDs are permanent: hosts store automation against them. Never rename one.
namespace id
{
    // Detector controls (section 4.2)
    inline constexpr const char* sourceType       = "sourceType";
    inline constexpr const char* kickSensitivity  = "kickSens";
    inline constexpr const char* snareSensitivity = "snareSens";
    inline constexpr const char* hatSensitivity   = "hatSens";
    inline constexpr const char* kickRelease      = "kickRelease";
    inline constexpr const char* snareRelease     = "snareRelease";
    inline constexpr const char* hatRelease       = "hatRelease";

    // Host automation surface (section 6). Declared now so the layout hosts see never changes;
    // the render engine starts reading them in phases 2 and 3.
    inline constexpr const char* intensity    = "intensity";
    inline constexpr const char* sceneSelect  = "scene";
    inline constexpr const char* moduleSelect = "module";
    inline constexpr const char* lookSelect   = "look";
    inline constexpr const char* flashLimiter = "flashLimiter";
    inline constexpr const char* freeze       = "freeze";
    inline constexpr int numMacros = 8;
    juce::String macro (int index);   // 0-based -> "macro1".."macro8"
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace cortex::params
