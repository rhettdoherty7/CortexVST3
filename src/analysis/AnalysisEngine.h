#pragma once

#include "AnalysisFrame.h"
#include "BandSplitter.h"
#include "OnsetDetector.h"
#include "PitchTracker.h"

#include <array>
#include <memory>
#include <vector>

namespace juce { namespace dsp { class FFT; } }

namespace cortex
{

/** Turns audio into AnalysisFrames (section 4).

    Feed it blocks of any size; every `hopSize()` samples it runs one FFT and hands a finished
    frame to the sink. The window is 2048 samples with a 512 hop at 44.1/48 kHz and doubles with
    the sample rate (4096/1024 at 88.2/96 kHz, 8192/2048 at 176.4/192 kHz), so frequency
    resolution and frame rate (~90 frames/s) stay the same everywhere.

    Real-time rules: prepare() allocates, process() never does, and nothing here locks. */
class AnalysisEngine
{
public:
    AnalysisEngine();
    ~AnalysisEngine();

    void prepare (double sampleRate);
    void reset();

    /** channels: 1 or 2 read-only channel pointers. The audio is only read, never written. */
    void process (const float* const* channels, int numChannels, int numSamples,
                  const HostTime& host, const AnalysisSettings& settings, FrameSink& sink) noexcept;

    /** Raw detector inputs for the frame just emitted. Feeds the "Solo View" tuning meters and the tests. */
    struct DetectorDebug
    {
        float kickFlux = 0, bodyFlux = 0, noiseFlux = 0, hatFlux = 0;
        float kickThreshold = 0, bodyThreshold = 0, noiseThreshold = 0, hatThreshold = 0;
        float lowRise = 0, bodyRise = 0, noiseRise = 0, hatRise = 0;   // linear amplitude rise per band
    };
    const DetectorDebug& detectorDebug() const noexcept   { return debug; }

    int    fftSize() const noexcept        { return fftLength; }
    int    hopSize() const noexcept        { return hopLength; }
    double frameSeconds() const noexcept   { return frameDuration; }

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void clear() noexcept { z1 = z2 = 0.0; }
    };

    /** One detector band: its FFT bins plus this frame's level change. */
    struct DetectorBand
    {
        int lo = 1, hi = 1;                     // inclusive bin range
        float amplitude = 0.0f, level = 0.0f;   // band amplitude, and the same log-compressed
        float flux = 0.0f;                      // rise in level since the last frame (drives the detector)
        float rise = 0.0f;                      // rise in amplitude since the last frame (compares bands)
    };

    /** A detector firing that still has to prove its band out-rises the neighbouring one.
        It gets the frame it fired on and the one after, with the rises added up. */
    struct Candidate
    {
        int age = 99;                                   // frames since the detector fired; >1 means none pending
        float strength = 0.0f, own = 0.0f, neighbour = 0.0f;

        void update (bool fired, float newStrength, float ownRise, float neighbourRise) noexcept
        {
            if (fired && age > 1)
            {
                age = 0;
                strength = newStrength;
                own = ownRise;
                neighbour = neighbourRise;
            }
            else if (age <= 1)
            {
                ++age;
                own += ownRise;
                neighbour += neighbourRise;
            }
        }

        /** True once, on the frame the candidate is accepted. */
        bool resolve (float requiredRatio) noexcept
        {
            if (age > 1 || own < neighbour * requiredRatio)
                return false;

            age = 99;
            return true;
        }
    };

    DetectorBand bandForHz (float loHz, float hiHz) const noexcept;
    void measure (DetectorBand&) const noexcept;
    void emitFrame (double framePpq, const HostTime&, const AnalysisSettings&, FrameSink&) noexcept;

    double sampleRate = 48000.0, frameDuration = 512.0 / 48000.0;
    int fftLength = 2048, hopLength = 512, ringMask = 2047, rateScale = 1;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> ringL, ringR, window, fftBuffer, magnitudes;
    int writeIndex = 0, hopCounter = 0;
    int64_t samplesProcessed = 0;

    // Per-hop accumulators
    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0, sumMid = 0.0, sumSide = 0.0, sumKWeighted = 0.0;
    float hopPeak = 0.0f;

    // Loudness (ITU-R BS.1770 K-weighting, 3 s short-term window)
    std::array<Biquad, 2> shelf, highpass;
    std::vector<double> loudnessHistory;
    int loudnessWrite = 0, loudnessCount = 0;

    BandSplitter bands;
    PitchTracker pitch;

    DetectorBand lowBand, bodyBand, noiseBand, hatBand;
    OnsetDetector kickDetector, bodyDetector, noiseDetector, hatDetector, broadDetector;
    int framesSinceBody = 1000, framesSinceNoise = 1000;
    Candidate kickCandidate, snareCandidate;
    float secondsSinceSnare = 1000.0f;
    float kickEnvelope = 0.0f, snareEnvelope = 0.0f, hatEnvelope = 0.0f;

    float smoothedWidth = 0.0f, smoothedCorrelation = 0.0f, stereoCoeff = 1.0f;
    double freePpq = 0.0, lastBarStartPpq = 0.0;
    DetectorDebug debug;
    bool pitchActive = false;
    unsigned pitchFrameCounter = 0;
    float lastPitchHz = 0.0f, lastPitchConfidence = 0.0f;

    // A drum only counts if it out-rises the band next to it (linear amplitude). Without this a
    // snare's body reads as a kick, and a hat's low end reads as a snare.
    static constexpr float kickOverBodyImmediate = 1.1f;
    static constexpr float kickOverBody = 0.85f;
    static constexpr float snareOverHat = 0.4f;
    static constexpr float hatOverNoise = 0.7f;
};

} // namespace cortex
