#pragma once

#include "FrameInterpolator.h"

#include <array>
#include <vector>

namespace cortex
{

/** Things that can move a parameter (section 6). */
enum class ModSource
{
    kick = 0, snare, hat,
    band0, band1, band2, band3, band4, band5, band6, band7,
    rms, flux, centroid, pitch, beatPhase, barPhase,
    lfo1,       // sine, one cycle per bar
    lfo2,       // triangle, one cycle per beat
    random,     // new value on every beat
    macro1, macro2, macro3, macro4, macro5, macro6, macro7, macro8,
    count
};

const char* modSourceName (ModSource) noexcept;

enum class ModCurve { linear = 0, exponential, logarithmic };

/** Something a route can move. Ranges are fixed per kind (see Renderer). */
struct ModTarget
{
    enum class Kind
    {
        widgetIntensity, widgetSpeed, widgetScale, widgetColorShift, widgetParam,
        sceneBloom, sceneExposure,
        fxFeedback, fxGlitch, fxChromatic      // the three always-present global effect slots
    };

    Kind kind = Kind::widgetIntensity;
    int widget = -1;     // -1: every widget
    int index = 0;       // widgetParam: which of p[0..15]
};

struct ModRoute
{
    ModSource source = ModSource::kick;
    ModTarget target;
    float amount = 0.5f;           // -1..1, as a fraction of the target's range
    ModCurve curve = ModCurve::linear;
    float smoothing = 0.0f;        // seconds
};

/** The 8 macro knobs' fixed assignments, added to every scene (docs/DECISIONS.md D-024). */
const std::vector<ModRoute>& macroRoutes() noexcept;
const char* macroLabel (int index) noexcept;   // 0-based

/** Evaluates routes once per render frame. setRoutes() allocates; update() never does. */
class ModRouter
{
public:
    void setRoutes (std::vector<ModRoute>);
    void reset() noexcept;

    void update (const RenderInputs&, const std::array<float, 8>& macros, float dt) noexcept;

    float source (ModSource s) const noexcept   { return sources[(size_t) s]; }

    /** Calls fn (const ModTarget&, float offset) for every route; offset is amount x curve(source),
        smoothed, in units of the target's range. */
    template <typename Fn>
    void forEach (Fn&& fn) const
    {
        for (size_t i = 0; i < routes.size(); ++i)
            fn (routes[i].target, outputs[i]);
    }

private:
    std::vector<ModRoute> routes;
    std::vector<float> outputs;
    std::array<float, (size_t) ModSource::count> sources {};
    float lastBeatPhase = 0.0f, randomValue = 0.5f, lastPitch = 0.0f;
    unsigned randomSeed = 0x9e3779b9u;
};

} // namespace cortex
