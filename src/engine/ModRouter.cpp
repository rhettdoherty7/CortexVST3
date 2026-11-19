#include "ModRouter.h"

#include <algorithm>
#include <cmath>

namespace cortex
{

const char* modSourceName (ModSource s) noexcept
{
    static const char* const names[] = {
        "Kick", "Snare", "Hat",
        "Sub", "Bass", "Low-mid", "Mid", "High-mid", "Presence", "Brilliance", "Air",
        "Level", "Flux", "Brightness", "Pitch", "Beat phase", "Bar phase",
        "LFO 1", "LFO 2", "Random",
        "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8",
    };
    static_assert (sizeof (names) / sizeof (names[0]) == (size_t) ModSource::count);
    return names[(size_t) std::clamp ((int) s, 0, (int) ModSource::count - 1)];
}

const char* macroLabel (int index) noexcept
{
    static const char* const labels[8] = { "Intensity", "Speed", "Scale", "Color", "Bloom", "Trails", "Glitch", "Aberration" };
    return labels[std::clamp (index, 0, 7)];
}

const std::vector<ModRoute>& macroRoutes() noexcept
{
    static const std::vector<ModRoute> routes = []
    {
        using K = ModTarget::Kind;
        const auto route = [] (ModSource s, K kind, float amount)
        {
            ModRoute r;
            r.source = s;
            r.target.kind = kind;
            r.amount = amount;
            r.smoothing = 0.05f;   // knob and automation moves glide instead of stepping
            return r;
        };

        return std::vector<ModRoute> {
            route (ModSource::macro1, K::widgetIntensity, 0.5f),    // up to 2x
            route (ModSource::macro2, K::widgetSpeed, 0.5f),        // up to 3x
            route (ModSource::macro3, K::widgetScale, 0.33f),
            route (ModSource::macro4, K::widgetColorShift, 0.8f),   // most of a turn: full is never the same as off
            route (ModSource::macro5, K::sceneBloom, 0.6f),
            route (ModSource::macro6, K::fxFeedback, 1.0f),
            route (ModSource::macro7, K::fxGlitch, 1.0f),
            route (ModSource::macro8, K::fxChromatic, 1.0f),
        };
    }();
    return routes;
}

void ModRouter::setRoutes (std::vector<ModRoute> newRoutes)
{
    routes = std::move (newRoutes);
    outputs.assign (routes.size(), 0.0f);
}

void ModRouter::reset() noexcept
{
    std::fill (outputs.begin(), outputs.end(), 0.0f);
    sources.fill (0.0f);
    lastBeatPhase = 0.0f;
    randomValue = 0.5f;
    lastPitch = 0.0f;
}

void ModRouter::update (const RenderInputs& in, const std::array<float, 8>& macros, float dt) noexcept
{
    constexpr float twoPi = 6.28318530718f;
    auto set = [this] (ModSource s, float v) { sources[(size_t) s] = std::clamp (v, 0.0f, 1.0f); };

    set (ModSource::kick, in.kick);
    set (ModSource::snare, in.snare);
    set (ModSource::hat, in.hat);
    for (int b = 0; b < 8; ++b)
        set ((ModSource) ((int) ModSource::band0 + b), in.bands[b]);
    set (ModSource::rms, in.rms * 2.0f);   // typical mixes sit around 0.1 to 0.4 RMS
    set (ModSource::flux, in.flux);
    set (ModSource::centroid, in.centroid);

    if (in.pitchConf > 0.0f && in.pitchHz > 0.0f)
        lastPitch = std::log2 (in.pitchHz / 30.0f) / std::log2 (1000.0f / 30.0f);   // 30 Hz..1 kHz
    set (ModSource::pitch, lastPitch);

    set (ModSource::beatPhase, in.beatPhase);
    set (ModSource::barPhase, in.barPhase);
    set (ModSource::lfo1, 0.5f - 0.5f * std::cos (in.barPhase * twoPi));
    set (ModSource::lfo2, 1.0f - std::abs (2.0f * in.beatPhase - 1.0f));

    if (in.beatPhase < lastBeatPhase - 0.5f)   // the beat wrapped
    {
        randomSeed = randomSeed * 1664525u + 1013904223u;
        randomValue = (float) (randomSeed >> 8) / 16777216.0f;
    }
    lastBeatPhase = in.beatPhase;
    set (ModSource::random, randomValue);

    for (int m = 0; m < 8; ++m)
        set ((ModSource) ((int) ModSource::macro1 + m), macros[(size_t) m]);

    for (size_t i = 0; i < routes.size(); ++i)
    {
        const auto& r = routes[i];
        float x = sources[(size_t) r.source];
        if (r.curve == ModCurve::exponential) x = x * x;
        if (r.curve == ModCurve::logarithmic) x = std::sqrt (x);

        const float target = r.amount * x;
        if (r.smoothing <= 0.0f || dt <= 0.0f)
            outputs[i] = target;
        else
            outputs[i] += (target - outputs[i]) * (1.0f - std::exp (-dt / r.smoothing));
    }
}

} // namespace cortex
