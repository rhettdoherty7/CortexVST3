#pragma once

#include <cstdint>
#include <type_traits>

namespace cortex
{

/** One analysis result, produced every hop on the audio thread and handed to the render thread
    through a lock-free FIFO. Plain data, fixed size, no pointers. Spec: CLAUDE.md section 4.1. */
struct AnalysisFrame
{
    double  ppq;            // host musical position in quarter notes, or -1 if the host isn't playing
    double  bpm;            // host tempo (120 until a host or tempo detector provides one)
    int64_t samplePos;      // samples processed since prepare(), at the end of this frame's window
    float   rms, peak, lufsShort;           // overall level (linear, linear, LUFS over the last 3 s)
    float   bands[8];       // sub, bass, low-mid, mid, high-mid, presence, brilliance, air (smoothed, 0..1)
    float   spectrum[128];  // log-spaced 20 Hz..20 kHz, dB-normalized 0..1
    float   waveL[256], waveR[256];         // decimated scope data, oldest first
    float   kick, snare, hat;               // onset envelopes 0..1 (instant attack, tunable release)
    bool    kickHit, snareHit, hatHit;      // single-frame triggers
    float   flux;           // spectral flux 0..1 (overall "something happened")
    float   centroid;       // brightness 0..1 (log frequency, 20 Hz..20 kHz)
    float   pitchHz, pitchConf;             // monophonic pitch (YIN); 0 unless the source type enables it
    float   stereoWidth, correlation;       // 0..1 and -1..1
    float   barPhase, beatPhase;            // 0..1, tempo-locked; free-runs when the host is stopped
};

static_assert(std::is_trivially_copyable_v<AnalysisFrame>, "AnalysisFrame must stay plain data");

/** What the instance is listening to. Re-weights the detectors (section 4.2, per-track mode). */
enum class SourceType { fullMix = 0, kick, snare, hats, bass, vocal, synth };

/** Host transport for the block being processed. */
struct HostTime
{
    double ppq         = -1.0;   // quarter notes at the first sample of the block, or -1 if unknown
    double barStartPpq = 0.0;    // quarter-note position of the start of the current bar
    double bpm         = 0.0;    // 0 if unknown
    int    timeSigNum  = 4;
    int    timeSigDen  = 4;
    bool   playing     = false;
};

/** User-facing detector controls, read once per block. */
struct AnalysisSettings
{
    float kickSensitivity  = 0.5f;   // 0..1
    float snareSensitivity = 0.5f;
    float hatSensitivity   = 0.5f;
    float kickRelease      = 0.18f;  // seconds
    float snareRelease     = 0.15f;
    float hatRelease       = 0.08f;
    SourceType source      = SourceType::fullMix;
};

/** Receives each finished frame. Called on the audio thread: must not block or allocate. */
struct FrameSink
{
    virtual ~FrameSink() = default;
    virtual void onFrame (const AnalysisFrame&) noexcept = 0;
};

} // namespace cortex
