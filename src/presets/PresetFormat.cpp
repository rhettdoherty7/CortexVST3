#include "PresetFormat.h"

#include <engine/Layers.h>
#include <engine/ModRouter.h>
#include <engine/ModuleInfo.h>

#include <algorithm>

namespace cortex
{

namespace
{
    // Names written to files. Never change one: old presets would stop loading.
    const char* const blendNames[]   = { "normal", "add", "screen", "multiply" };
    const char* const tonemapNames[] = { "film", "punchy", "neutral" };
    const char* const curveNames[]   = { "linear", "exponential", "logarithmic" };
    const char* const layerIds[]     = { "bloom", "grain", "vignette", "chromatic", "scanlines", "feedback", "glitch", "gradientMap" };
    const char* const targetKinds[]  = { "widgetIntensity", "widgetSpeed", "widgetScale", "widgetColorShift", "widgetParam",
                                         "sceneBloom", "sceneExposure", "fxFeedback", "fxGlitch", "fxChromatic" };
    const char* const sourceIds[]    = { "kick", "snare", "hat", "band1", "band2", "band3", "band4", "band5", "band6", "band7", "band8",
                                         "rms", "flux", "centroid", "pitch", "beatPhase", "barPhase", "lfo1", "lfo2", "random",
                                         "macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7", "macro8" };
    static_assert (sizeof (layerIds) / sizeof (layerIds[0]) == (size_t) LayerType::count);
    static_assert (sizeof (sourceIds) / sizeof (sourceIds[0]) == (size_t) ModSource::count);

    template <size_t N>
    int indexOf (const char* const (&names)[N], const juce::String& name, int fallback)
    {
        for (size_t i = 0; i < N; ++i)
            if (name == names[i])
                return (int) i;
        return fallback;
    }

    juce::var floats (const float* values, size_t count)
    {
        juce::Array<juce::var> list;
        for (size_t i = 0; i < count; ++i)
            list.add (std::round (values[i] * 10000.0f) / 10000.0f);   // keep files readable
        return list;
    }

    void readFloats (const juce::var& v, float* out, size_t count)
    {
        if (const auto* list = v.getArray())
            for (size_t i = 0; i < count && i < (size_t) list->size(); ++i)
                out[i] = (float) (double) (*list)[(int) i];
    }

    float number (const juce::var& object, const char* key, float fallback)
    {
        const auto v = object.getProperty (key, {});
        return v.isVoid() ? fallback : (float) (double) v;
    }

    juce::String hex (uint32_t colour)
    {
        return "#" + juce::String::toHexString ((int) colour).paddedLeft ('0', 6).toUpperCase();
    }
}

juce::var presetToVar (const ScenePreset& preset)
{
    const auto& s = preset.scene;
    auto* root = new juce::DynamicObject();
    root->setProperty ("schema", presetSchema);
    root->setProperty ("name", juce::String (s.name));

    juce::Array<juce::var> tags;
    for (const auto& t : s.tags)
        tags.add (juce::String (t));
    root->setProperty ("tags", tags);

    root->setProperty ("look", juce::String (factoryLook (preset.look).name));
    root->setProperty ("background", s.backgroundPalette);
    root->setProperty ("tonemap", tonemapNames[(size_t) s.tonemap]);
    root->setProperty ("exposure", s.exposure);
    root->setProperty ("bloom", s.bloom);
    root->setProperty ("crossfadeBeats", s.crossfadeBeats);

    if (preset.cover)
    {
        juce::Array<juce::var> colours;
        for (auto c : preset.cover->srgbHex)
            colours.add (hex (c));
        root->setProperty ("coverPalette", colours);
    }

    juce::Array<juce::var> widgets;
    for (const auto& w : s.widgets)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("module", juce::String (w.module));
        const float rect[] = { w.x, w.y, w.w, w.h };
        o->setProperty ("rect", floats (rect, 4));
        o->setProperty ("blend", blendNames[(size_t) w.blend]);
        o->setProperty ("opacity", w.opacity);
        o->setProperty ("intensity", w.settings.intensity);
        o->setProperty ("speed", w.settings.speed);
        o->setProperty ("scale", w.settings.scale);
        o->setProperty ("colorShift", w.settings.colorShift);
        o->setProperty ("palette", w.settings.palette);
        o->setProperty ("params", floats (w.settings.p.data(), w.settings.p.size()));

        juce::Array<juce::var> layers;
        for (const auto& l : w.layers)
        {
            auto* lo = new juce::DynamicObject();
            lo->setProperty ("type", layerIds[(size_t) l.type]);
            lo->setProperty ("amount", l.amount);
            lo->setProperty ("params", floats (l.p.data(), l.p.size()));
            if (l.palette >= 0)
                lo->setProperty ("palette", l.palette);
            layers.add (juce::var (lo));
        }
        o->setProperty ("layers", layers);
        widgets.add (juce::var (o));
    }
    root->setProperty ("widgets", widgets);

    juce::Array<juce::var> routes;
    for (const auto& r : s.routes)
    {
        auto* ro = new juce::DynamicObject();
        ro->setProperty ("source", sourceIds[(size_t) r.source]);
        ro->setProperty ("target", targetKinds[(size_t) r.target.kind]);
        ro->setProperty ("widget", r.target.widget);
        ro->setProperty ("index", r.target.index);
        ro->setProperty ("amount", r.amount);
        ro->setProperty ("curve", curveNames[(size_t) r.curve]);
        ro->setProperty ("smoothing", r.smoothing);
        routes.add (juce::var (ro));
    }
    root->setProperty ("routes", routes);

    return juce::var (root);
}

std::optional<ScenePreset> presetFromVar (const juce::var& root, juce::String& error)
{
    if (! root.isObject())
    {
        error = "not a Cortex preset (expected a JSON object)";
        return std::nullopt;
    }

    const int schema = (int) root.getProperty ("schema", 0);
    if (schema < 1)
    {
        error = "not a Cortex preset (no schema number)";
        return std::nullopt;
    }
    if (schema > presetSchema)
    {
        error = "made by a newer version of Cortex (schema " + juce::String (schema) + ")";
        return std::nullopt;
    }
    // Migrations from older schemas go here, one step at a time.

    ScenePreset preset;
    auto& s = preset.scene;
    s.name = root.getProperty ("name", "Untitled").toString().toStdString();
    if (const auto* tags = root.getProperty ("tags", {}).getArray())
        for (const auto& t : *tags)
            s.tags.push_back (t.toString().toStdString());

    const auto lookName = root.getProperty ("look", "None").toString();
    for (int i = 0; i < numFactoryLooks(); ++i)
        if (lookName == factoryLook (i).name)
            preset.look = i;

    s.backgroundPalette = std::clamp ((int) root.getProperty ("background", 0), 0, numFactoryPalettes() - 1);
    s.tonemap = (Tonemap) indexOf (tonemapNames, root.getProperty ("tonemap", "film").toString(), 0);
    s.exposure = std::clamp (number (root, "exposure", 1.0f), 0.25f, 4.0f);
    s.bloom = std::clamp (number (root, "bloom", 0.08f), 0.0f, 0.5f);
    s.crossfadeBeats = std::clamp (number (root, "crossfadeBeats", 1.0f), 0.0f, 16.0f);

    if (const auto* colours = root.getProperty ("coverPalette", {}).getArray(); colours != nullptr && colours->size() == 6)
    {
        Palette p;
        p.name = "Cover Art";
        for (int i = 0; i < 6; ++i)
            p.srgbHex[(size_t) i] = (uint32_t) (*colours)[i].toString().trimCharactersAtStart ("#").getHexValue32();
        preset.cover = p;
    }

    if (const auto* widgets = root.getProperty ("widgets", {}).getArray())
    {
        for (const auto& wv : *widgets)
        {
            const auto module = wv.getProperty ("module", "").toString().toStdString();
            if (findModuleInfo (module) == nullptr)
                continue;   // a module this version does not have: skip that widget, keep the rest

            WidgetSpec w = defaultWidget (module, 0);
            float rect[] = { 0.0f, 0.0f, 1.0f, 1.0f };
            readFloats (wv.getProperty ("rect", {}), rect, 4);
            w.x = std::clamp (rect[0], 0.0f, 1.0f);
            w.y = std::clamp (rect[1], 0.0f, 1.0f);
            w.w = std::clamp (rect[2], 0.02f, 1.0f - w.x);
            w.h = std::clamp (rect[3], 0.02f, 1.0f - w.y);
            w.blend = (BlendMode) indexOf (blendNames, wv.getProperty ("blend", "normal").toString(), 0);
            w.opacity = std::clamp (number (wv, "opacity", 1.0f), 0.0f, 1.0f);
            w.settings.intensity = std::clamp (number (wv, "intensity", 1.0f), 0.0f, 2.0f);
            w.settings.speed = std::clamp (number (wv, "speed", 1.0f), 0.0f, 4.0f);
            w.settings.scale = std::clamp (number (wv, "scale", 1.0f), 0.25f, 3.0f);
            w.settings.colorShift = std::clamp (number (wv, "colorShift", 0.0f), 0.0f, 1.0f);
            w.settings.palette = std::clamp ((int) wv.getProperty ("palette", 0), 0, numFactoryPalettes() - 1);
            readFloats (wv.getProperty ("params", {}), w.settings.p.data(), w.settings.p.size());

            if (const auto* layers = wv.getProperty ("layers", {}).getArray())
            {
                for (const auto& lv : *layers)
                {
                    const int type = indexOf (layerIds, lv.getProperty ("type", "").toString(), -1);
                    if (type < 0)
                        continue;
                    LayerSpec l;
                    l.type = (LayerType) type;
                    l.amount = std::clamp (number (lv, "amount", 0.5f), 0.0f, 1.0f);
                    readFloats (lv.getProperty ("params", {}), l.p.data(), l.p.size());
                    l.palette = std::clamp ((int) lv.getProperty ("palette", -1), -1, numFactoryPalettes() - 1);
                    w.layers.push_back (l);
                }
            }
            s.widgets.push_back (std::move (w));
        }
    }

    if (const auto* routes = root.getProperty ("routes", {}).getArray())
    {
        for (const auto& rv : *routes)
        {
            const int source = indexOf (sourceIds, rv.getProperty ("source", "").toString(), -1);
            const int kind = indexOf (targetKinds, rv.getProperty ("target", "").toString(), -1);
            if (source < 0 || kind < 0)
                continue;
            ModRoute r;
            r.source = (ModSource) source;
            r.target.kind = (ModTarget::Kind) kind;
            r.target.widget = std::clamp ((int) rv.getProperty ("widget", -1), -1, (int) s.widgets.size() - 1);
            r.target.index = std::clamp ((int) rv.getProperty ("index", 0), 0, 15);
            r.amount = std::clamp (number (rv, "amount", 0.5f), -1.0f, 1.0f);
            r.curve = (ModCurve) indexOf (curveNames, rv.getProperty ("curve", "linear").toString(), 0);
            r.smoothing = std::clamp (number (rv, "smoothing", 0.0f), 0.0f, 2.0f);
            s.routes.push_back (r);
        }
    }

    if (s.widgets.empty())
    {
        error = "the preset has no widgets this version of Cortex can draw";
        return std::nullopt;
    }
    return preset;
}

juce::String presetToJson (const ScenePreset& preset)
{
    return juce::JSON::toString (presetToVar (preset), false, 4);
}

std::optional<ScenePreset> presetFromJson (const juce::String& json, juce::String& error)
{
    juce::var root;
    const auto result = juce::JSON::parse (json, root);
    if (result.failed())
    {
        error = "not valid JSON: " + result.getErrorMessage();
        return std::nullopt;
    }
    return presetFromVar (root, error);
}

ScenePreset factoryPreset (int sceneIndex)
{
    ScenePreset p;
    p.scene = factoryScene (sceneIndex);
    return p;
}

} // namespace cortex
