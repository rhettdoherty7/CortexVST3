#include "Scene.h"

#include <algorithm>
#include <initializer_list>

namespace cortex
{

namespace
{
    // Palette indices into factoryPalette() (engine/Palette.cpp).
    enum Pal { cortexPal = 0, nightDrive, ember, glacier, acid, roseGold, deepSea, ultraviolet, sodium, monoIce, infrared, terminal };

    WidgetSpec widget (const char* module, int palette, std::initializer_list<float> params,
                       float x = 0.0f, float y = 0.0f, float w = 1.0f, float h = 1.0f,
                       BlendMode blend = BlendMode::normal)
    {
        WidgetSpec spec;
        spec.module = module;
        spec.x = x; spec.y = y; spec.w = w; spec.h = h;
        spec.blend = blend;
        spec.settings.palette = palette;
        std::copy (params.begin(), params.end(), spec.settings.p.begin());
        return spec;
    }

    SceneSpec scene (const char* name, int background, std::vector<WidgetSpec> widgets, float bloom,
                     Tonemap tonemap = Tonemap::film)
    {
        SceneSpec s;
        s.name = name;
        s.backgroundPalette = background;
        s.widgets = std::move (widgets);
        s.bloom = bloom;
        s.tonemap = tonemap;
        return s;
    }

    const std::vector<SceneSpec>& scenes()
    {
        static const std::vector<SceneSpec> list = []
        {
            std::vector<SceneSpec> v;

            // Bloom Orb: p0 rays, p1 flare
            v.push_back (scene ("Pulse", cortexPal, { widget ("bloom_orb", cortexPal, { 0.6f, 0.8f }) }, 0.10f));

            // Spectrum Bars: p0 layout (0 linear, 1 mirrored, 2 radial), p1 bar count, p2 gap
            v.push_back (scene ("Spectrum", nightDrive, { widget ("spectrum_bars", nightDrive, { 1.0f, 0.55f, 0.35f }) }, 0.10f));

            // Scope: p0 mode (0 time, 1 X/Y), p1 persistence, p2 gain
            v.push_back (scene ("Phosphor", terminal, { widget ("scope", terminal, { 0.0f, 0.15f, 0.35f }) }, 0.14f));

            // Waveform Stack: p0 gain
            v.push_back (scene ("Strata", glacier, { widget ("waveform_stack", glacier, { 0.5f }) }, 0.10f));

            // Flowfield: p0 field scale, p1 flow speed, p2 trail length, p3 brightness
            v.push_back (scene ("Drift", ultraviolet, { widget ("flowfield", ultraviolet, { 0.45f, 0.5f, 0.6f, 0.5f }) }, 0.12f));

            // Pulse Grid: p0 rows, p1 base density
            v.push_back (scene ("Signal", sodium, { widget ("pulse_grid", sodium, { 0.45f, 0.5f }) }, 0.12f));

            // Three widgets: the Phase 2 performance case.
            v.push_back (scene ("Undertow", deepSea,
                                { widget ("flowfield", deepSea, { 0.35f, 0.4f, 0.7f, 0.35f }),
                                  widget ("bloom_orb", deepSea, { 0.5f, 0.6f }, 0.0f, 0.0f, 1.0f, 1.0f, BlendMode::add),
                                  widget ("spectrum_bars", deepSea, { 0.0f, 0.75f, 0.3f }, 0.0f, 0.62f, 1.0f, 0.38f, BlendMode::add) },
                                0.12f));
            return v;
        }();

        return list;
    }
}

int numFactoryScenes() noexcept
{
    return (int) scenes().size();
}

const SceneSpec& factoryScene (int index) noexcept
{
    const auto& list = scenes();
    return list[(size_t) std::clamp (index, 0, (int) list.size() - 1)];
}

} // namespace cortex
