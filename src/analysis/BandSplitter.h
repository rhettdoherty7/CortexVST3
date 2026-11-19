#pragma once

#include <array>

namespace cortex
{

/** Turns an FFT magnitude spectrum into the 8 smoothed band levels and the 128-point log spectrum
    of AnalysisFrame. No allocation after prepare(). */
class BandSplitter
{
public:
    static constexpr int numBands    = 8;
    static constexpr int numSpectrum = 128;

    void prepare (double sampleRate, int fftSize, double frameSeconds);
    void reset();

    /** magnitudes: fftSize/2 + 1 values, scaled so a full-scale sine reads 1.0. */
    void process (const float* magnitudes, float* bandsOut, float* spectrumOut) noexcept;

private:
    struct SpectrumPoint
    {
        int   lo = 0, hi = 0;   // inclusive bin range; lo < 0 means interpolate at `position`
        float position = 0.0f;  // fractional bin of the point's centre frequency
    };

    std::array<int, numBands> bandLo {}, bandHi {};
    std::array<SpectrumPoint, numSpectrum> points {};
    std::array<float, numBands> bandState {};
    std::array<float, numSpectrum> spectrumState {};
    float attackCoeff = 1.0f, releaseCoeff = 1.0f, spectrumReleaseCoeff = 1.0f;
    int numBins = 0;
};

} // namespace cortex
