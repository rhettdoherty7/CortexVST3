#include "BandSplitter.h"

#include <algorithm>
#include <cmath>

namespace cortex
{

namespace
{
    // sub, bass, low-mid, mid, high-mid, presence, brilliance, air
    constexpr float bandEdgesHz[BandSplitter::numBands + 1] = { 20.0f, 60.0f, 250.0f, 500.0f, 2000.0f,
                                                                4000.0f, 6000.0f, 12000.0f, 20000.0f };

    constexpr float attackSeconds          = 0.005f;
    constexpr float releaseSeconds         = 0.120f;
    constexpr float spectrumReleaseSeconds = 0.080f;

    float onePoleCoeff (float seconds, double frameSeconds)
    {
        return 1.0f - std::exp (-(float) frameSeconds / seconds);
    }

    float normalisedDb (float linear, float floorDb)
    {
        const float db = 20.0f * std::log10 (linear + 1.0e-9f);
        return std::clamp ((db - floorDb) / -floorDb, 0.0f, 1.0f);
    }
}

void BandSplitter::prepare (double sampleRate, int fftSize, double frameSeconds)
{
    numBins = fftSize / 2 + 1;
    const float binHz   = (float) sampleRate / (float) fftSize;
    const int   lastBin = numBins - 1;

    for (int b = 0; b < numBands; ++b)
    {
        const int lo = std::clamp ((int) std::ceil (bandEdgesHz[b] / binHz), 1, lastBin);
        const int hi = std::clamp ((int) std::ceil (bandEdgesHz[b + 1] / binHz) - 1, lo, lastBin);
        bandLo[(size_t) b] = lo;
        bandHi[(size_t) b] = hi;
    }

    const float topHz = std::min (20000.0f, (float) sampleRate * 0.5f);
    const float ratio = topHz / 20.0f;

    for (int i = 0; i < numSpectrum; ++i)
    {
        const float f0 = 20.0f * std::pow (ratio, (float) i / (float) numSpectrum);
        const float f1 = 20.0f * std::pow (ratio, (float) (i + 1) / (float) numSpectrum);
        auto& p = points[(size_t) i];
        p.position = std::clamp (std::sqrt (f0 * f1) / binHz, 0.0f, (float) lastBin);
        p.lo = std::clamp ((int) std::ceil (f0 / binHz), 1, lastBin);
        p.hi = std::clamp ((int) std::ceil (f1 / binHz) - 1, 0, lastBin);
        if (p.hi <= p.lo)   // narrower than one bin: interpolate instead
            p.lo = p.hi = -1;
    }

    attackCoeff          = onePoleCoeff (attackSeconds, frameSeconds);
    releaseCoeff         = onePoleCoeff (releaseSeconds, frameSeconds);
    spectrumReleaseCoeff = onePoleCoeff (spectrumReleaseSeconds, frameSeconds);
    reset();
}

void BandSplitter::reset()
{
    bandState.fill (0.0f);
    spectrumState.fill (0.0f);
}

void BandSplitter::process (const float* mag, float* bandsOut, float* spectrumOut) noexcept
{
    // A Hann-windowed sine spreads over three bins (0.5, 1, 0.5); undo that so a full-scale sine reads 0 dB.
    constexpr float hannSpread = 1.0f / 1.2247449f;

    for (int b = 0; b < numBands; ++b)
    {
        float sum = 0.0f;
        for (int k = bandLo[(size_t) b]; k <= bandHi[(size_t) b]; ++k)
            sum += mag[k] * mag[k];

        const float target = normalisedDb (std::sqrt (sum) * hannSpread, -60.0f);
        float& state = bandState[(size_t) b];
        state += (target - state) * (target > state ? attackCoeff : releaseCoeff);
        bandsOut[b] = state;
    }

    for (int i = 0; i < numSpectrum; ++i)
    {
        const auto& p = points[(size_t) i];
        float value;

        if (p.lo < 0)
        {
            const int   k    = std::min ((int) p.position, numBins - 2);
            const float frac = p.position - (float) k;
            value = mag[k] + (mag[k + 1] - mag[k]) * frac;
        }
        else
        {
            value = 0.0f;
            for (int k = p.lo; k <= p.hi; ++k)
                value = std::max (value, mag[k]);
        }

        const float target = normalisedDb (value, -80.0f);
        float& state = spectrumState[(size_t) i];
        state = target > state ? target : state + (target - state) * spectrumReleaseCoeff;
        spectrumOut[i] = state;
    }
}

} // namespace cortex
