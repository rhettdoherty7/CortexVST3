#include <engine/ModuleInfo.h>
#include <presets/PresetFormat.h>
#include <presets/PresetManager.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cortex;
using Catch::Approx;

namespace
{
    void requireSame (const ScenePreset& a, const ScenePreset& b)
    {
        REQUIRE (a.scene.widgets.size() == b.scene.widgets.size());
        CHECK (a.scene.name == b.scene.name);
        CHECK (a.scene.tags == b.scene.tags);
        CHECK (a.look == b.look);
        CHECK (a.scene.backgroundPalette == b.scene.backgroundPalette);
        CHECK (a.scene.tonemap == b.scene.tonemap);
        CHECK (a.scene.bloom == Approx (b.scene.bloom).margin (1.0e-4));
        CHECK (a.cover.has_value() == b.cover.has_value());
        if (a.cover && b.cover)
            CHECK (a.cover->srgbHex == b.cover->srgbHex);

        for (size_t i = 0; i < a.scene.widgets.size(); ++i)
        {
            const auto& x = a.scene.widgets[i];
            const auto& y = b.scene.widgets[i];
            CHECK (x.module == y.module);
            CHECK (x.blend == y.blend);
            CHECK (x.x == Approx (y.x).margin (1.0e-4));
            CHECK (x.h == Approx (y.h).margin (1.0e-4));
            CHECK (x.settings.palette == y.settings.palette);
            CHECK (x.settings.speed == Approx (y.settings.speed).margin (1.0e-4));
            for (size_t k = 0; k < x.settings.p.size(); ++k)
                CHECK (x.settings.p[k] == Approx (y.settings.p[k]).margin (1.0e-4));
            REQUIRE (x.layers.size() == y.layers.size());
            for (size_t k = 0; k < x.layers.size(); ++k)
            {
                CHECK (x.layers[k].type == y.layers[k].type);
                CHECK (x.layers[k].amount == Approx (y.layers[k].amount).margin (1.0e-4));
                CHECK (x.layers[k].palette == y.layers[k].palette);
            }
        }

        REQUIRE (a.scene.routes.size() == b.scene.routes.size());
        for (size_t i = 0; i < a.scene.routes.size(); ++i)
        {
            CHECK (a.scene.routes[i].source == b.scene.routes[i].source);
            CHECK (a.scene.routes[i].target.kind == b.scene.routes[i].target.kind);
            CHECK (a.scene.routes[i].target.widget == b.scene.routes[i].target.widget);
            CHECK (a.scene.routes[i].amount == Approx (b.scene.routes[i].amount).margin (1.0e-4));
            CHECK (a.scene.routes[i].curve == b.scene.routes[i].curve);
        }
    }
}

TEST_CASE ("Every factory scene survives a trip through a .cortex file", "[presets]")
{
    for (int i = 0; i < numFactoryScenes(); ++i)
    {
        const auto original = factoryPreset (i);
        juce::String error;
        const auto back = presetFromJson (presetToJson (original), error);
        INFO (original.scene.name << ": " << error);
        REQUIRE (back.has_value());
        requireSame (original, *back);
    }
}

TEST_CASE ("Layers, routes, a Look and a cover palette are kept", "[presets]")
{
    auto preset = factoryPreset (0);
    preset.look = 3;
    Palette cover;
    cover.srgbHex = { 0x112233, 0x445566, 0x778899, 0xaabbcc, 0xddeeff, 0x010203 };
    preset.cover = cover;

    LayerSpec l;
    l.type = LayerType::gradientMap;
    l.amount = 0.7f;
    l.palette = 4;
    preset.scene.widgets[0].layers.push_back (l);
    preset.scene.widgets[0].blend = BlendMode::screen;

    ModRoute r;
    r.source = ModSource::lfo2;
    r.target.kind = ModTarget::Kind::widgetParam;
    r.target.widget = 0;
    r.target.index = 1;
    r.amount = -0.4f;
    r.curve = ModCurve::exponential;
    preset.scene.routes.push_back (r);

    juce::String error;
    const auto back = presetFromJson (presetToJson (preset), error);
    REQUIRE (back.has_value());
    requireSame (preset, *back);
}

TEST_CASE ("Broken or foreign presets are refused with a reason", "[presets]")
{
    juce::String error;
    CHECK_FALSE (presetFromJson ("not json at all {", error).has_value());
    CHECK (error.isNotEmpty());

    error.clear();
    CHECK_FALSE (presetFromJson ("{\"name\": \"x\"}", error).has_value());   // no schema
    CHECK (error.contains ("schema"));

    error.clear();
    CHECK_FALSE (presetFromJson ("{\"schema\": 99, \"widgets\": []}", error).has_value());
    CHECK (error.contains ("newer"));
}

TEST_CASE ("A widget with an unknown module is skipped, the rest still load", "[presets]")
{
    const juce::String json = "{ \"schema\": 1, \"name\": \"Mixed\", \"widgets\": ["
                              "{ \"module\": \"hologram_from_the_future\" },"
                              "{ \"module\": \"bloom_orb\", \"rect\": [0.1, 0.1, 0.5, 0.5], \"params\": [0.2, 0.9] } ] }";
    juce::String error;
    const auto p = presetFromJson (json, error);
    REQUIRE (p.has_value());
    REQUIRE (p->scene.widgets.size() == 1);
    CHECK (p->scene.widgets[0].module == "bloom_orb");
    CHECK (p->scene.widgets[0].settings.p[1] == Approx (0.9f));
}

TEST_CASE ("Every module has a description for the inspector", "[presets]")
{
    for (const auto& info : moduleInfos())
    {
        INFO (info.id);
        CHECK (std::string (info.name).size() > 0);
        CHECK (info.params.size() <= 16);
        const auto w = defaultWidget (info.id, 2);
        CHECK (w.settings.palette == 2);
        for (size_t i = 0; i < info.params.size(); ++i)
            CHECK (w.settings.p[i] == info.params[i].defaultValue);
    }
}

TEST_CASE ("The preset library saves, filters, favourites and deletes", "[presets]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("cortex_preset_test_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    const auto folder = root.getChildFile ("Presets");
    {
        PresetManager manager (folder);
        const int factoryCount = numFactoryScenes();
        CHECK ((int) manager.entries().size() == factoryCount);

        auto preset = factoryPreset (1);
        preset.scene.tags = { "Night", "Trap" };
        juce::String error;
        const int index = manager.save (preset, "My Night Drive", error);
        REQUIRE (index >= 0);
        CHECK ((int) manager.entries().size() == factoryCount + 1);
        CHECK (folder.getChildFile ("My Night Drive.cortex").existsAsFile());

        CHECK (manager.filter ("night drive", {}, false).size() == 1);
        CHECK (manager.filter ("", { "Night" }, false).size() == 1);
        CHECK (manager.filter ("", {}, true).empty());

        manager.setFavourite (manager.entries()[(size_t) index], true);
        manager.setFavourite (manager.entries()[0], true);
        CHECK (manager.filter ("", {}, true).size() == 2);

        const auto loaded = manager.load (manager.entries()[(size_t) index], error);
        REQUIRE (loaded.has_value());
        CHECK (loaded->scene.name == "My Night Drive");
    }
    {
        PresetManager reopened (folder);   // favourites persist
        CHECK (reopened.filter ("", {}, true).size() == 2);

        const auto entries = reopened.entries();
        for (const auto& e : entries)
            if (! e.factory)
                CHECK (reopened.remove (e));
        CHECK ((int) reopened.entries().size() == numFactoryScenes());
        CHECK (reopened.filter ("", {}, true).size() == 1);
    }
    root.deleteRecursively();
}
