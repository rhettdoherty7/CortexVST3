#include "PitchTracker.h"

#include <algorithm>
#include <cmath>

namespace cortex
{

void PitchTracker::prepare (double sampleRate)
{
    decimation        = std::max (1, (int) std::lround (sampleRate / 12000.0));
    decimatedRate     = sampleRate / decimation;
    tauMin            = std::max (2, (int) (decimatedRate / 1000.0));
    tauMax            = (int) (decimatedRate / 30.0);
    integrationLength = (int) (decimatedRate * 0.04);

    const int needed = integrationLength + tauMax + 1;
    int ringSize = 1;
    while (ringSize < needed)
        ringSize <<= 1;

    ring.assign ((size_t) ringSize, 0.0f);
    ringMask = ringSize - 1;
    window.assign ((size_t) needed, 0.0f);
    difference.assign ((size_t) tauMax + 1, 0.0f);
    reset();
}

void PitchTracker::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writeIndex = 0;
    accumulator = 0.0f;
    accumulatorCount = 0;
}

void PitchTracker::analyse (float& hz, float& confidence) noexcept
{
    hz = 0.0f;
    confidence = 0.0f;

    const int n = (int) window.size();
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = ring[(size_t) ((writeIndex - n + i) & ringMask)];

    float energy = 0.0f;
    for (int j = 0; j < integrationLength; ++j)
        energy += window[(size_t) j] * window[(size_t) j];

    if (energy < 1.0e-6f * (float) integrationLength)   // quieter than about -60 dBFS
        return;

    // Difference function, then cumulative-mean normalisation (YIN steps 2 and 3).
    difference[0] = 1.0f;
    float runningSum = 0.0f;
    constexpr float yinThreshold = 0.15f;
    int best = -1;

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < integrationLength; ++j)
        {
            const float d = window[(size_t) j] - window[(size_t) (j + tau)];
            sum += d * d;
        }

        runningSum += sum;
        const float normalised = runningSum > 0.0f ? sum * (float) tau / runningSum : 1.0f;
        difference[(size_t) tau] = normalised;

        // Take the first dip under the threshold and follow it down to its local minimum. Stopping
        // there skips the rest of the lags, which is most of the work for anything above ~60 Hz.
        if (tau < tauMin)
            continue;

        if (best < 0)
        {
            if (normalised < yinThreshold)
                best = tau;
        }
        else if (normalised < difference[(size_t) best])
        {
            best = tau;
        }
        else
        {
            break;
        }
    }

    if (best < 0)   // nothing clearly periodic: fall back to the global minimum
    {
        best = tauMin;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (difference[(size_t) tau] < difference[(size_t) best])
                best = tau;
    }

    confidence = std::clamp (1.0f - difference[(size_t) best], 0.0f, 1.0f);
    if (confidence < 0.5f)
    {
        confidence = 0.0f;
        return;
    }

    // Parabolic interpolation around the minimum for sub-sample period accuracy.
    float period = (float) best;
    if (best > 1 && best < tauMax)
    {
        const float a = difference[(size_t) best - 1];
        const float b = difference[(size_t) best];
        const float c = difference[(size_t) best + 1];
        const float denominator = a - 2.0f * b + c;
        if (std::abs (denominator) > 1.0e-9f)
            period += 0.5f * (a - c) / denominator;
    }

    hz = (float) decimatedRate / period;
}

} // namespace cortex
