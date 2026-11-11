#include "ModuleInfo.h"

namespace cortex
{

const std::vector<ModuleInfo>& moduleInfos() noexcept
{
    static const std::vector<ModuleInfo> list = {
        { "bloom_orb", "Bloom Orb", "A glowing core: level sets its size, kicks send out a ring, snares flare.",
          { { "Rays", 0.6f, {} }, { "Flare", 0.8f, {} } } },
        { "spectrum_bars", "Spectrum Bars", "The spectrum as bars: straight, mirrored or around a circle.",
          { { "Layout", 1.0f, { "Linear", "Mirrored", "Radial" } }, { "Bars", 0.55f, {} }, { "Gap", 0.35f, {} } } },
        { "scope", "Scope", "A phosphor oscilloscope: the waveform over time, or left against right.",
          { { "Mode", 0.0f, { "Time", "X/Y" } }, { "Persistence", 0.15f, {} }, { "Gain", 0.35f, {} } } },
        { "waveform_stack", "Waveform Stack", "Recent waveforms stacked into the distance, eight rows a beat.",
          { { "Gain", 0.5f, {} } } },
        { "flowfield", "Flowfield", "100,000 particles on a swirling current; kicks blast them outward.",
          { { "Field size", 0.45f, {} }, { "Flow speed", 0.5f, {} }, { "Trails", 0.6f, {} }, { "Brightness", 0.5f, {} } } },
        { "pulse_grid", "Pulse Grid", "A field of glyphs lit by the spectrum; hats reshuffle the characters.",
          { { "Rows", 0.45f, {} }, { "Density", 0.5f, {} } } },
    };
    return list;
}

const ModuleInfo* findModuleInfo (const std::string& id) noexcept
{
    for (const auto& info : moduleInfos())
        if (id == info.id)
            return &info;
    return nullptr;
}

WidgetSpec defaultWidget (const std::string& moduleId, int palette)
{
    WidgetSpec w;
    w.module = moduleId;
    w.settings.palette = palette;
    if (const auto* info = findModuleInfo (moduleId))
        for (size_t i = 0; i < info->params.size() && i < w.settings.p.size(); ++i)
            w.settings.p[i] = info->params[i].defaultValue;
    return w;
}

} // namespace cortex
