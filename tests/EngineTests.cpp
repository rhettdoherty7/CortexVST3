#include <engine/FrameInterpolator.h>
#include <engine/Palette.h>
#include <engine/Scene.h>
#include <safety/FlashLimiter.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <deque>
#include <string>

using namespace cortex;
using Catch::Approx;

// --- Colour --------------------------------------------------------------------------------------

TEST_CASE ("sRGB and OKLab conversions round-trip", "[colour]")
{
    for (float v : { 0.0f, 0.02f, 0.2f, 0.5f, 0.9f, 1.0f })
        CHECK (linearToSrgb (srgbToLinear (v)) == Approx (v).margin (1.0e-5));

    for (const Rgb c : { Rgb { 1, 0, 0 }, Rgb { 0.2f, 0.7f, 0.1f }, Rgb { 0.05f, 0.05f, 0.9f }, Rgb { 1, 1, 1 } })
    {
        const Rgb back = oklabToLinear (linearToOklab (c));
        CHECK (back.r == Approx (c.r).margin (1.0e-4));
        CHECK (back.g == Approx (c.g).margin (1.0e-4));
        CHECK (back.b == Approx (c.b).margin (1.0e-4));
    }

    // White is L = 1 with no colour.
    const Rgb white = linearToOklab ({ 1, 1, 1 });
    CHECK (white.r == Approx (1.0f).margin (1.0e-3));
    CHECK (white.g == Approx (0.0f).margin (1.0e-3));
    CHECK (white.b == Approx (0.0f).margin (1.0e-3));
}

TEST_CASE ("An OKLab midpoint is not muddier than its ends", "[colour]")
{
    // Mixing blue and yellow in plain RGB gives grey; in OKLab the midpoint keeps its lightness.
    const Rgb blue { 0.0f, 0.0f, 1.0f }, yellow { 1.0f, 1.0f, 0.0f };
    const float lBlue = linearToOklab (blue).r, lYellow = linearToOklab (yellow).r;
    const float lMid = linearToOklab (mixOklab (blue, yellow, 0.5f)).r;
    CHECK (lMid == Approx (0.5f * (lBlue + lYellow)).margin (1.0e-3));
}

TEST_CASE ("Factory palettes and scenes are well formed", "[colour][scene]")
{
    for (int i = 0; i < numFactoryPalettes(); ++i)
    {
        const auto& p = factoryPalette (i);
        INFO (p.name);
        for (int c = 0; c < 4; ++c)   // colours run dark to bright
            CHECK (linearToOklab (p.linear (c)).r < linearToOklab (p.linear (c + 1)).r);
        CHECK (linearToOklab (p.linear (5)).r < 0.2f);   // backgrounds are near black
    }

    REQUIRE (numFactoryScenes() >= 7);
    bool hasThreeWidgets = false;
    for (int i = 0; i < numFactoryScenes(); ++i)
    {
        const auto& s = factoryScene (i);
        CHECK_FALSE (s.widgets.empty());
        hasThreeWidgets = hasThreeWidgets || s.widgets.size() >= 3;
        for (const auto& w : s.widgets)
        {
            CHECK (w.x >= 0.0f);
            CHECK (w.y >= 0.0f);
            CHECK (w.x + w.w <= 1.0001f);
            CHECK (w.y + w.h <= 1.0001f);
            CHECK (w.settings.palette < numFactoryPalettes());
        }
    }
    CHECK (hasThreeWidgets);
}

// --- Flash limiter -------------------------------------------------------------------------------

namespace
{
    /** Counts what a viewer sees: a flash is a rise of `threshold` or more in whole-frame luminance
        after a fall (WCAG 2.3.1, counted conservatively on rises alone). Returns the most flashes
        seen inside any one-second window. */
    struct ViewerCounter
    {
        float threshold = 0.1f;
        bool high = false, primed = false;
        float extreme = 0.0f;
        double now = 0.0;
        std::deque<double> rises;
        int worst = 0;

        void observe (float luminance, float dt)
        {
            now += dt;
            if (! primed) { extreme = luminance; primed = true; return; }

            if (! high)
            {
                extreme = std::min (extreme, luminance);
                if (luminance - extreme >= threshold) { high = true; extreme = luminance; rises.push_back (now); }
            }
            else
            {
                extreme = std::max (extreme, luminance);
                if (extreme - luminance >= threshold) { high = false; extreme = luminance; }
            }

            while (! rises.empty() && rises.front() <= now - 1.0)
                rises.pop_front();
            worst = std::max (worst, (int) rises.size());
        }
    };

    int runStrobe (FlashLimiter::Mode mode, float hz, float seconds, float low = 0.0f, float high = 1.0f)
    {
        FlashLimiter limiter;
        ViewerCounter viewer;
        const float dt = 1.0f / 60.0f;
        float lum[16], red[16] = {};

        for (int frame = 0; frame < (int) (seconds * 60.0f); ++frame)
        {
            const float t = (float) frame * dt;
            const float value = std::fmod (t * hz, 1.0f) < 0.5f ? high : low;
            std::fill (std::begin (lum), std::end (lum), value);

            const auto result = limiter.process (lum, red, dt, mode);
            viewer.observe (value * result.gain, dt);
        }
        return viewer.worst;
    }
}

TEST_CASE ("The flash limiter holds a strobe to three flashes a second", "[safety]")
{
    for (float hz : { 4.0f, 8.0f, 15.0f, 30.0f })
    {
        INFO (hz << " Hz strobe");
        CHECK (runStrobe (FlashLimiter::Mode::off, hz, 4.0f) > 3);       // proves the strobe is a real hazard
        CHECK (runStrobe (FlashLimiter::Mode::standard, hz, 4.0f) <= 3);
    }

    // Strict allows two.
    CHECK (runStrobe (FlashLimiter::Mode::strict, 10.0f, 4.0f) <= 2);
}

TEST_CASE ("The flash limiter leaves safe content alone", "[safety]")
{
    FlashLimiter limiter;
    float lum[16], red[16] = {};
    float lowestGain = 1.0f;

    // Two flashes a second, then a slow fade: neither needs limiting.
    for (int frame = 0; frame < 360; ++frame)
    {
        const float t = (float) frame / 60.0f;
        const float value = frame < 180 ? (std::fmod (t * 2.0f, 1.0f) < 0.5f ? 0.9f : 0.1f) : 0.5f + 0.4f * std::sin (t);
        std::fill (std::begin (lum), std::end (lum), value);
        lowestGain = std::min (lowestGain, limiter.process (lum, red, 1.0f / 60.0f, FlashLimiter::Mode::standard).gain);
    }
    CHECK (lowestGain == 1.0f);
}

TEST_CASE ("A flash over less than a quarter of the picture does not count", "[safety]")
{
    FlashLimiter limiter;
    float red[16] = {};
    float lowestGain = 1.0f;

    for (int frame = 0; frame < 240; ++frame)
    {
        float lum[16] = {};
        const bool on = (frame / 2) % 2 == 0;   // 15 Hz
        for (int c = 0; c < 3; ++c)             // 3 of 16 cells
            lum[c] = on ? 1.0f : 0.0f;
        lowestGain = std::min (lowestGain, limiter.process (lum, red, 1.0f / 60.0f, FlashLimiter::Mode::standard).gain);
    }
    CHECK (lowestGain == 1.0f);
}

TEST_CASE ("Saturated red flashes are limited", "[safety]")
{
    FlashLimiter limiter;
    float lum[16] = {}, red[16];
    float strongest = 0.0f;

    for (int frame = 0; frame < 240; ++frame)
    {
        std::fill (std::begin (red), std::end (red), (frame / 3) % 2 == 0 ? 0.6f : 0.0f);
        strongest = std::max (strongest, limiter.process (lum, red, 1.0f / 60.0f, FlashLimiter::Mode::standard).redDesaturation);
    }
    CHECK (strongest > 0.5f);
}

// --- Frame interpolator --------------------------------------------------------------------------

namespace
{
    AnalysisFrame steadyFrame()
    {
        AnalysisFrame f {};
        f.bpm = 120.0;
        f.rms = 0.5f;
        for (auto& b : f.bands) b = 0.6f;
        return f;
    }
}

TEST_CASE ("Interpolated values do not depend on the display frame rate", "[interpolator]")
{
    const auto settle = [] (int hz)
    {
        FrameInterpolator interpolator;
        const float dt = 1.0f / (float) hz;
        RenderInputs result;
        for (int i = 0; i < hz / 10; ++i)   // 100 ms
        {
            interpolator.push (steadyFrame());
            result = interpolator.update (dt);
        }
        return result;
    };

    const auto at60 = settle (60), at144 = settle (144), at240 = settle (240);
    CHECK (at60.rms == Approx (at144.rms).margin (0.02));
    CHECK (at144.rms == Approx (at240.rms).margin (0.02));
    CHECK (at60.bands[3] == Approx (at240.bands[3]).margin (0.02));

    // Critically damped: rises toward the target and never past it.
    CHECK (at240.rms <= 0.5f);
    CHECK (at240.rms > 0.4f);
}

TEST_CASE ("A hit shows for exactly one render frame", "[interpolator]")
{
    FrameInterpolator interpolator;
    auto frame = steadyFrame();
    frame.kickHit = true;
    frame.kick = 1.0f;

    interpolator.push (frame);
    const auto first = interpolator.update (1.0f / 60.0f);
    CHECK (first.kickHit);
    CHECK (first.kick == Approx (1.0f));
    CHECK (first.sinceKick == 0.0f);

    frame.kickHit = false;
    interpolator.push (frame);
    const auto second = interpolator.update (1.0f / 60.0f);
    CHECK_FALSE (second.kickHit);
    CHECK (second.sinceKick > 0.0f);
    CHECK (second.kickCount == 1.0f);
}

TEST_CASE ("Two hits between render frames still both count", "[interpolator]")
{
    FrameInterpolator interpolator;
    auto frame = steadyFrame();
    frame.snareHit = true;
    interpolator.push (frame);
    frame.snareHit = false;
    interpolator.push (frame);   // the hit is in an older frame than the newest

    CHECK (interpolator.update (1.0f / 60.0f).snareHit);
}

TEST_CASE ("With no audio the idle animation keeps moving", "[interpolator]")
{
    FrameInterpolator interpolator;
    float lowest = 1.0f, highest = 0.0f;
    for (int i = 0; i < 240; ++i)
    {
        const auto& in = interpolator.update (1.0f / 60.0f);
        CHECK (in.idle);
        lowest = std::min (lowest, in.kick);
        highest = std::max (highest, in.kick);
    }
    CHECK (highest - lowest > 0.2f);
}

// --- Modulation ----------------------------------------------------------------------------------

#include <engine/Layers.h>
#include <engine/ModRouter.h>
#include <presets/CoverArtPalette.h>

namespace
{
    std::vector<float> routeOutputs (const ModRouter& router)
    {
        std::vector<float> out;
        router.forEach ([&out] (const ModTarget&, float v) { out.push_back (v); });
        return out;
    }
}

TEST_CASE ("Routes follow their source through the curve", "[modulation]")
{
    ModRouter router;
    ModRoute linear, squared, root;
    linear.source = squared.source = root.source = ModSource::kick;
    linear.amount = squared.amount = root.amount = 1.0f;
    squared.curve = ModCurve::exponential;
    root.curve = ModCurve::logarithmic;
    router.setRoutes ({ linear, squared, root });

    RenderInputs in;
    in.kick = 0.25f;
    router.update (in, {}, 1.0f / 60.0f);

    const auto out = routeOutputs (router);
    REQUIRE (out.size() == 3);
    CHECK (out[0] == Approx (0.25f));
    CHECK (out[1] == Approx (0.0625f));
    CHECK (out[2] == Approx (0.5f));
}

TEST_CASE ("Route smoothing glides at the same rate at any frame rate", "[modulation]")
{
    const auto after = [] (int hz)
    {
        ModRouter router;
        ModRoute r;
        r.source = ModSource::macro1;
        r.amount = 1.0f;
        r.smoothing = 0.1f;
        router.setRoutes ({ r });

        std::array<float, 8> macros {};
        macros[0] = 1.0f;
        for (int i = 0; i < hz / 10; ++i)   // 100 ms
            router.update ({}, macros, 1.0f / (float) hz);
        return routeOutputs (router)[0];
    };

    CHECK (after (60) == Approx (1.0f - std::exp (-1.0f)).margin (0.01));
    CHECK (after (60) == Approx (after (240)).margin (0.01));
}

TEST_CASE ("Tempo LFOs and the random source follow the beat", "[modulation]")
{
    ModRouter router;
    router.setRoutes ({});

    RenderInputs in;
    in.barPhase = 0.5f;
    in.beatPhase = 0.5f;
    router.update (in, {}, 0.01f);
    CHECK (router.source (ModSource::lfo1) == Approx (1.0f));   // a bar's sine peaks half-way
    CHECK (router.source (ModSource::lfo2) == Approx (1.0f));   // a beat's triangle peaks half-way

    in.beatPhase = 0.9f;
    router.update (in, {}, 0.01f);
    const float before = router.source (ModSource::random);
    in.beatPhase = 0.95f;
    router.update (in, {}, 0.01f);
    CHECK (router.source (ModSource::random) == before);        // holds within a beat

    in.beatPhase = 0.02f;                                        // the beat wraps
    router.update (in, {}, 0.01f);
    CHECK (router.source (ModSource::random) != before);
}

TEST_CASE ("Every macro has a fixed route and a label", "[modulation]")
{
    const auto& routes = macroRoutes();
    REQUIRE (routes.size() == 8);
    for (int m = 0; m < 8; ++m)
    {
        CHECK (routes[(size_t) m].source == (ModSource) ((int) ModSource::macro1 + m));
        CHECK (std::string (macroLabel (m)).size() > 0);
    }
}

TEST_CASE ("Factory Looks are well formed", "[looks]")
{
    REQUIRE (numFactoryLooks() >= 8);
    CHECK (factoryLook (0).layers.empty());   // "None"
    for (int i = 1; i < numFactoryLooks(); ++i)
    {
        const auto& look = factoryLook (i);
        INFO (look.name);
        CHECK_FALSE (look.layers.empty());
        for (const auto& l : look.layers)
        {
            CHECK (l.amount > 0.0f);
            CHECK (l.amount <= 1.0f);
            CHECK ((l.type == LayerType::bloom || layerShaderFile (l.type) != nullptr));
            CHECK (l.palette < numFactoryPalettes());
        }
    }
}

// --- Cover art -----------------------------------------------------------------------------------

namespace
{
    std::vector<Rgb> blocks (std::initializer_list<Rgb> colours, int each = 400)
    {
        std::vector<Rgb> out;
        for (const auto& c : colours)
            for (int i = 0; i < each; ++i)
                out.push_back (c);
        return out;
    }

    float hueDegrees (const Rgb& lab) { return std::atan2 (lab.b, lab.g) * 57.29578f; }
}

TEST_CASE ("A cover-art palette runs dark to bright over a near-black background", "[coverart]")
{
    const auto pixels = blocks ({ { 0.9f, 0.1f, 0.1f }, { 0.1f, 0.6f, 0.2f }, { 0.1f, 0.2f, 0.9f },
                                  { 0.95f, 0.85f, 0.2f }, { 0.95f, 0.95f, 0.95f }, { 0.05f, 0.04f, 0.06f } });
    const auto p = paletteFromPixels (pixels);

    for (int c = 0; c < 4; ++c)
        CHECK (p.oklab (c).r < p.oklab (c + 1).r);
    CHECK (p.oklab (5).r < 0.2f);

    // Deterministic.
    CHECK (paletteFromPixels (pixels).srgbHex == p.srgbHex);
}

TEST_CASE ("A red cover gives a red palette", "[coverart]")
{
    // Shades of red, from dark to light.
    std::vector<Rgb> pixels;
    for (int i = 0; i < 3000; ++i)
    {
        const float t = (float) (i % 100) / 99.0f;
        pixels.push_back ({ 0.25f + 0.7f * t, 0.05f + 0.15f * t * t, 0.05f + 0.12f * t * t });
    }
    const auto p = paletteFromPixels (pixels);

    const float redHue = hueDegrees (linearToOklab ({ 1.0f, 0.0f, 0.0f }));
    for (int c = 0; c < 4; ++c)   // the lightest may be close to white, where hue means little
    {
        INFO ("colour " << c << " hue " << hueDegrees (p.oklab (c)) << ", red is " << redHue);
        CHECK (std::abs (hueDegrees (p.oklab (c)) - redHue) < 30.0f);
    }
}

TEST_CASE ("A flat or empty cover still gives a usable palette", "[coverart]")
{
    const auto flat = paletteFromPixels (blocks ({ { 0.3f, 0.5f, 0.7f } }));
    for (int c = 0; c < 4; ++c)
        CHECK (flat.oklab (c).r < flat.oklab (c + 1).r);

    const auto empty = paletteFromPixels ({});
    CHECK (empty.srgbHex == factoryPalette (0).srgbHex);
}
