#include "FrameInterpolator.h"

#include <algorithm>
#include <cmath>

namespace cortex
{

namespace
{
    constexpr float twoPi = 6.28318530718f;
    constexpr float levelOmega = twoPi * 6.0f;   // level springs settle in roughly 60 ms
    constexpr float idleAfterSeconds = 0.5f;

    double fraction (double x) noexcept { return x - std::floor (x); }
}

// Critically damped spring, exact for any dt (closed form, so it is frame-rate independent).
void FrameInterpolator::Spring::step (float target, float omega, float dt) noexcept
{
    const float decay = std::exp (-omega * dt);
    const float delta = value - target;
    const float temp  = (velocity + omega * delta) * dt;
    velocity = (velocity - omega * temp) * decay;
    value    = target + (delta + temp) * decay;
}

void FrameInterpolator::reset() noexcept
{
    *this = FrameInterpolator();
}

void FrameInterpolator::push (const AnalysisFrame& f) noexcept
{
    target = f;
    pendingKick  = pendingKick  || f.kickHit;
    pendingSnare = pendingSnare || f.snareHit;
    pendingHat   = pendingHat   || f.hatHit;
    gotFrame = true;
    secondsSinceFrame = 0.0f;
}

void FrameInterpolator::idleTarget (AnalysisFrame& f, float dt) noexcept
{
    // A calm, slow breathing pattern at 90 BPM with a soft pulse on each beat.
    idleClock += dt;
    const auto t = (float) idleClock;
    const double beat = idleClock * 1.5;
    const auto beatPhase = (float) fraction (beat);

    f = {};
    f.bpm = 90.0;
    f.ppq = -1.0;
    f.rms = 0.10f + 0.05f * std::sin (t * 0.7f);
    f.centroid = 0.45f + 0.15f * std::sin (t * 0.23f);
    f.beatPhase = beatPhase;
    f.barPhase = (float) fraction (beat / 4.0);
    f.kick = 0.45f * std::exp (-beatPhase * 5.0f);
    f.correlation = 0.6f;
    f.stereoWidth = 0.3f;

    for (int b = 0; b < 8; ++b)
        f.bands[b] = 0.30f + 0.18f * std::sin (t * (0.31f + 0.07f * (float) b) + (float) b * 1.3f);

    for (int i = 0; i < 128; ++i)
    {
        const float x = (float) i / 127.0f;
        f.spectrum[i] = std::clamp (0.55f * (1.0f - 0.6f * x)
                                    + 0.16f * std::sin (x * 9.0f + t * 0.9f)
                                    + 0.10f * std::sin (x * 23.0f - t * 1.4f), 0.0f, 1.0f);
    }

    for (int i = 0; i < 256; ++i)
    {
        const float x = (float) i / 255.0f;
        f.waveL[i] = 0.22f * std::sin (x * twoPi * 2.0f + t * 2.1f) + 0.08f * std::sin (x * twoPi * 7.0f - t * 3.3f);
        f.waveR[i] = 0.22f * std::sin (x * twoPi * 2.0f + t * 2.1f + 0.9f) + 0.08f * std::sin (x * twoPi * 5.0f + t * 2.7f);
    }
}

const RenderInputs& FrameInterpolator::update (float dt, float speed) noexcept
{
    dt = std::clamp (dt, 0.0f, 0.1f);
    secondsSinceFrame += dt;
    out.idle = secondsSinceFrame > idleAfterSeconds;

    if (out.idle)
    {
        idleTarget (target, dt);
        gotFrame = true;
    }

    out.dt = dt * speed;
    out.time += out.dt;
    out.bpm = target.bpm;

    // --- Hits and envelopes: instant attack, analysis-rate release kept alive locally -----------
    const auto envelope = [dt] (float& env, float targetEnv, bool hit, float& since, float& count, bool& flag)
    {
        env = std::max (env * std::exp (-dt / 0.12f), targetEnv);
        flag = hit;
        since = hit ? 0.0f : std::min (since + dt, 100.0f);
        count += hit ? 1.0f : 0.0f;
    };

    envelope (out.kick,  target.kick,  pendingKick,  out.sinceKick,  out.kickCount,  out.kickHit);
    envelope (out.snare, target.snare, pendingSnare, out.sinceSnare, out.snareCount, out.snareHit);
    envelope (out.hat,   target.hat,   pendingHat,   out.sinceHat,   out.hatCount,   out.hatHit);
    pendingKick = pendingSnare = pendingHat = false;

    // --- Levels on springs ----------------------------------------------------------------------
    rms.step (target.rms, levelOmega, dt);
    flux.step (target.flux, levelOmega * 2.0f, dt);
    centroid.step (target.centroid, levelOmega * 0.5f, dt);
    width.step (target.stereoWidth, levelOmega * 0.5f, dt);
    correlation.step (target.correlation, levelOmega * 0.5f, dt);

    out.rms = std::max (rms.value, 0.0f);
    out.flux = std::clamp (flux.value, 0.0f, 1.0f);
    out.centroid = std::clamp (centroid.value, 0.0f, 1.0f);
    out.stereoWidth = std::clamp (width.value, 0.0f, 1.0f);
    out.correlation = std::clamp (correlation.value, -1.0f, 1.0f);

    for (int b = 0; b < 8; ++b)
    {
        bands[b].step (target.bands[b], levelOmega, dt);
        out.bands[b] = std::clamp (bands[b].value, 0.0f, 1.0f);
    }

    // Spectrum: quick up, slower down, per bin.
    const float up = 1.0f - std::exp (-dt / 0.015f), down = 1.0f - std::exp (-dt / 0.09f);
    for (int i = 0; i < 128; ++i)
        out.spectrum[i] += (target.spectrum[i] - out.spectrum[i]) * (target.spectrum[i] > out.spectrum[i] ? up : down);

    // The scope shows the newest audio as it is.
    if (gotFrame)
    {
        std::copy (std::begin (target.waveL), std::end (target.waveL), std::begin (out.waveL));
        std::copy (std::begin (target.waveR), std::end (target.waveR), std::begin (out.waveR));
    }

    out.pitchHz = target.pitchHz;
    out.pitchConf = target.pitchConf;

    // --- Musical time: run forward at the tempo between analysis frames, re-sync when one lands --
    if (gotFrame)
    {
        // Snap the fractional part to the analysis frame, keep counting whole beats ourselves.
        double whole = std::floor (beats);
        const double frac = fraction (beats);
        if (target.beatPhase < 0.25f && frac > 0.75)      whole += 1.0;   // analysis wrapped first
        else if (target.beatPhase > 0.75f && frac < 0.25) whole -= 1.0;   // we wrapped first
        beats = whole + target.beatPhase;
        barPhaseNow = target.barPhase;
    }
    else
    {
        beats += (double) dt * target.bpm / 60.0;
        barPhaseNow = (float) fraction ((double) barPhaseNow + (double) dt * target.bpm / 240.0);
    }

    out.beatPhase = (float) fraction (beats);
    out.barPhase = barPhaseNow;
    gotFrame = false;

    // Louder music moves things faster, but the drift never stops or jumps.
    out.motion += out.dt * (0.35f + 1.6f * out.rms + 0.5f * out.kick);
    return out;
}

} // namespace cortex
