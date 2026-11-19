#pragma once

#include <engine/Palette.h>
#include <engine/Scene.h>

#include <juce_core/juce_core.h>

#include <optional>

namespace cortex
{

/** Everything a `.cortex` preset holds (section 11, docs/PRESET_FORMAT.md). */
struct ScenePreset
{
    SceneSpec scene;
    int look = 0;                        // index into factoryLook()
    std::optional<Palette> cover;        // a cover-art palette that replaces the widgets' palettes
};

inline constexpr int presetSchema = 1;

/** To and from the JSON shape of a `.cortex` file. */
juce::var presetToVar (const ScenePreset&);
std::optional<ScenePreset> presetFromVar (const juce::var&, juce::String& error);

juce::String presetToJson (const ScenePreset&);
std::optional<ScenePreset> presetFromJson (const juce::String&, juce::String& error);

/** A factory scene as a preset. */
ScenePreset factoryPreset (int sceneIndex);

} // namespace cortex
