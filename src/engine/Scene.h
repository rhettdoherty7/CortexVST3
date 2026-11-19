#pragma once

#include "Layers.h"
#include "ModRouter.h"

#include <array>
#include <string>
#include <vector>

namespace cortex
{

enum class Tonemap { film = 0, punchy, neutral };      // AgX, ACES fitted, Khronos PBR Neutral
enum class BlendMode { normal = 0, add, screen, multiply };

/** Per-widget look controls (section 5.2: every module has Intensity, Speed, Scale, Color Shift). */
struct WidgetSettings
{
    float intensity = 1.0f, speed = 1.0f, scale = 1.0f, colorShift = 0.0f;
    std::array<float, 16> p {};    // module-specific parameters, meaning documented in each shader
    int palette = 0;
    bool lockPalette = false;      // true: ignore a cover-art palette override (Looks with fixed colours)

    // Filled in by the renderer each frame: this widget's own clocks, which advance at its Speed,
    // so changing Speed changes the rate of motion without making anything jump.
    float clock = 0.0f, motionClock = 0.0f;
};

/** One widget in a scene description (section 5.1). Rotation is not supported yet. */
struct WidgetSpec
{
    std::string module;
    float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;   // fraction of the canvas, origin top-left
    BlendMode blend = BlendMode::normal;
    float opacity = 1.0f;
    WidgetSettings settings;
    std::vector<LayerSpec> layers;  // this widget's own effects, applied before compositing
};

struct SceneSpec
{
    std::string name;
    std::vector<std::string> tags;  // for the preset browser: module, mood, genre
    std::vector<WidgetSpec> widgets;
    int backgroundPalette = 0;      // the palette whose background colour fills the canvas
    Tonemap tonemap = Tonemap::film;
    float exposure = 1.0f;
    float bloom = 0.08f;            // bloom mixed into the picture, 0..1
    std::vector<ModRoute> routes;   // scene modulation, in addition to the macro routes
    float crossfadeBeats = 1.0f;    // how long a change to this scene takes to fade in
};

/** Factory scenes, built once. Index 0 is the default. */
int numFactoryScenes() noexcept;
const SceneSpec& factoryScene (int index) noexcept;

} // namespace cortex
