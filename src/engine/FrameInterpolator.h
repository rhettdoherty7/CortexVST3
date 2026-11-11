#pragma once

#include <analysis/AnalysisFrame.h>

namespace cortex
{

/** Everything a module can react to, smoothed to the display's frame rate. */
struct RenderInputs
{
    float time = 0.0f;          // seconds of render time (scaled by the Speed control upstream)
    float dt = 0.0f;
    float motion = 0.0f;        // time that runs faster when the music is louder: steady drift for shaders

    float kick = 0.0f, snare = 0.0f, hat = 0.0f;                    // envelopes 0..1
    float sinceKick = 10.0f, sinceSnare = 10.0f, sinceHat = 10.0f;  // seconds since the last hit
    float kickCount = 0.0f, snareCount = 0.0f, hatCount = 0.0f;     // running hit counters
    bool  kickHit = false, snareHit = false, hatHit = false;        // true for the one render frame a hit lands

    float rms = 0.0f, flux = 0.0f, centroid = 0.0f;
    float bands[8] = {};
    float spectrum[128] = {};
    float waveL[256] = {}, waveR[256] = {};
    float beatPhase = 0.0f, barPhase = 0.0f;
    float pitchHz = 0.0f, pitchConf = 0.0f;
    float stereoWidth = 0.0f, correlation = 0.0f;
    double bpm = 120.0;
    bool  idle = true;          // no audio is arriving: values come from the idle animation
};

/** Turns the ~90 analysis frames a second into smooth per-display-frame values (section 3).
    Continuous values ride critically damped springs, so they never jitter or overshoot; hit
    envelopes keep their instant attack. With no audio it plays a slow idle animation so the
    picture never looks dead (section 10, empty state). */
class FrameInterpolator
{
public:
    void reset() noexcept;

    /** Call for every analysis frame that arrived since the last update(), oldest first. */
    void push (const AnalysisFrame&) noexcept;

    /** Advance by one display frame. */
    const RenderInputs& update (float dtSeconds, float speed = 1.0f) noexcept;

    const RenderInputs& current() const noexcept   { return out; }

private:
    struct Spring
    {
        float value = 0.0f, velocity = 0.0f;
        void step (float target, float omega, float dt) noexcept;
    };

    void idleTarget (AnalysisFrame&, float dt) noexcept;

    RenderInputs out;
    AnalysisFrame target {};
    bool pendingKick = false, pendingSnare = false, pendingHat = false;
    bool gotFrame = false;
    float secondsSinceFrame = 10.0f;
    double beats = 0.0, idleClock = 0.0;
    float barPhaseNow = 0.0f;

    Spring rms, flux, centroid, width, correlation;
    Spring bands[8];
};

} // namespace cortex
