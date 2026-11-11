#include "Layers.h"

#include <algorithm>

namespace cortex
{

const char* layerName (LayerType type) noexcept
{
    switch (type)
    {
        case LayerType::bloom:       return "Bloom";
        case LayerType::grain:       return "Film Grain";
        case LayerType::vignette:    return "Vignette";
        case LayerType::chromatic:   return "Chromatic";
        case LayerType::scanlines:   return "Scanlines";
        case LayerType::feedback:    return "Feedback";
        case LayerType::glitch:      return "Glitch";
        case LayerType::gradientMap: return "Gradient Map";
        case LayerType::count:       break;
    }
    return "";
}

const char* layerShaderFile (LayerType type) noexcept
{
    switch (type)
    {
        case LayerType::grain:       return "layer_grain.frag.glsl";
        case LayerType::vignette:    return "layer_vignette.frag.glsl";
        case LayerType::chromatic:   return "layer_chromatic.frag.glsl";
        case LayerType::scanlines:   return "layer_scanlines.frag.glsl";
        case LayerType::feedback:    return "layer_feedback.frag.glsl";
        case LayerType::glitch:      return "layer_glitch.frag.glsl";
        case LayerType::gradientMap: return "layer_gradient_map.frag.glsl";
        case LayerType::bloom:
        case LayerType::count:       break;
    }
    return nullptr;
}

namespace
{
    // Palette indices into factoryPalette() (engine/Palette.cpp).
    enum Pal { cortexPal = 0, nightDrive, ember, glacier, acid, roseGold, deepSea, ultraviolet, sodium, monoIce, infrared, terminal };

    LayerSpec layer (LayerType type, float amount, std::array<float, 4> p = { 0.5f, 0.5f, 0.5f, 0.5f }, int palette = -1)
    {
        LayerSpec l;
        l.type = type;
        l.amount = amount;
        l.p = p;
        l.palette = palette;
        return l;
    }

    const std::vector<LookSpec>& looks()
    {
        using T = LayerType;
        static const std::vector<LookSpec> list = {
            { "None", {} },
            { "Club Neon",    { layer (T::bloom, 0.8f, { 0.3f }),
                                layer (T::gradientMap, 0.35f, { 0.4f }),
                                layer (T::chromatic, 0.5f, { 0.9f }),
                                layer (T::vignette, 0.45f, { 0.45f, 0.5f }) } },
            { "VHS Tape",     { layer (T::scanlines, 0.55f, { 0.2f, 0.4f }),
                                layer (T::chromatic, 0.7f, { 0.3f }),
                                layer (T::glitch, 0.25f, { 0.3f, 0.6f }),
                                layer (T::grain, 0.55f, { 0.6f, 0.5f }),
                                layer (T::vignette, 0.55f, { 0.4f, 0.6f }) } },
            { "Night Vision", { layer (T::gradientMap, 1.0f, { 0.45f }, terminal),
                                layer (T::grain, 0.8f, { 0.4f, 0.0f }),
                                layer (T::scanlines, 0.3f, { 0.7f, 0.0f }),
                                layer (T::vignette, 0.8f, { 0.3f, 0.3f }) } },
            { "Blueprint",    { layer (T::gradientMap, 1.0f, { 0.3f }, glacier),
                                layer (T::grain, 0.25f, { 0.2f, 0.0f }),
                                layer (T::vignette, 0.35f, { 0.5f, 0.6f }) } },
            { "Mono Laser",   { layer (T::bloom, 1.0f, { 0.5f }),
                                layer (T::feedback, 0.45f, { 0.3f, 0.5f, 0.4f }),
                                layer (T::gradientMap, 1.0f, { 0.7f }, monoIce) } },
            { "Heat Map",     { layer (T::gradientMap, 1.0f, { 0.5f }, infrared),
                                layer (T::grain, 0.2f, { 0.3f, 0.2f }) } },
            { "Ghost Trails", { layer (T::feedback, 0.75f, { 0.55f, 0.6f, 0.7f }),
                                layer (T::chromatic, 0.3f, { 0.6f }),
                                layer (T::vignette, 0.3f) } },
            { "Terminal",     { layer (T::gradientMap, 0.9f, { 0.55f }, terminal),
                                layer (T::scanlines, 0.6f, { 0.6f, 0.5f }),
                                layer (T::glitch, 0.15f, { 0.7f, 0.5f }),
                                layer (T::grain, 0.3f, { 0.2f, 0.0f }) } },
        };
        return list;
    }
}

int numFactoryLooks() noexcept
{
    return (int) looks().size();
}

const LookSpec& factoryLook (int index) noexcept
{
    const auto& list = looks();
    return list[(size_t) std::clamp (index, 0, (int) list.size() - 1)];
}

} // namespace cortex
