#pragma once

#include <array>
#include <vector>

namespace cortex
{

/** Post effects (section 5.4). Bloom drives the renderer's bloom chain; the rest are passes. */
enum class LayerType { bloom = 0, grain, vignette, chromatic, scanlines, feedback, glitch, gradientMap, count };

struct LayerSpec
{
    LayerType type = LayerType::vignette;
    float amount = 0.5f;                                  // 0 switches the layer off
    std::array<float, 4> p { 0.5f, 0.5f, 0.5f, 0.5f };    // meaning documented in each layer shader
    int palette = -1;                                     // gradient map: fixed palette, or -1 for the scene's
};

/** A Look is a saved global layer stack (section 5.5). */
struct LookSpec
{
    const char* name = "";
    std::vector<LayerSpec> layers;
};

const char* layerName (LayerType) noexcept;
const char* layerShaderFile (LayerType) noexcept;   // nullptr for bloom

/** Index 0 is "None". */
int numFactoryLooks() noexcept;
const LookSpec& factoryLook (int index) noexcept;

} // namespace cortex
