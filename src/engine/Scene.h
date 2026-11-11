#pragma once

#include <array>
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
};

/** One widget in a scene description (section 5.1). Rotation is not supported yet. */
struct WidgetSpec
{
    const char* module = "";
    float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;   // fraction of the canvas, origin top-left
    BlendMode blend = BlendMode::normal;
    float opacity = 1.0f;
    WidgetSettings settings;
};

struct SceneSpec
{
    const char* name = "";
    std::vector<WidgetSpec> widgets;
    int backgroundPalette = 0;      // the palette whose background colour fills the canvas
    Tonemap tonemap = Tonemap::film;
    float exposure = 1.0f;
    float bloom = 0.08f;            // bloom mixed into the picture, 0..1
};

/** Factory scenes, built once. Index 0 is the default. */
int numFactoryScenes() noexcept;
const SceneSpec& factoryScene (int index) noexcept;

} // namespace cortex
