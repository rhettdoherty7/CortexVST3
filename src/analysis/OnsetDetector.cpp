#include "OnsetDetector.h"

#include <algorithm>
#include <cmath>

namespace cortex
{

void OnsetDetector::prepare (double newFrameSeconds)
{
    frameSeconds  = (float) newFrameSeconds;
    historyLength = std::clamp ((int) std::lround (0.5 / newFrameSeconds), 8, maxHistory);
    peakDecay     = std::exp (-frameSeconds / 0.15f);
    reset();
}

void OnsetDetector::reset()
{
    history.fill (0.0f);
    historyCount = 0;
    historyWrite = 0;
    secondsSinceHit = 1000.0f;
    recentPeak = 0.0f;
    lastStrength = 0.0f;
    lastThreshold = 0.0f;
}

float OnsetDetector::median() noexcept
{
    if (historyCount == 0)
        return 0.0f;

    std::copy_n (history.begin(), historyCount, scratch.begin());
    const auto mid = scratch.begin() + historyCount / 2;
    std::nth_element (scratch.begin(), mid, scratch.begin() + historyCount);
    return *mid;
}

bool OnsetDetector::process (float flux, float sensitivity, float minRetriggerSeconds) noexcept
{
    const float s          = std::clamp (sensitivity, 0.0f, 1.0f);
    const float multiplier = 3.5f + (1.4f - 3.5f) * s;
    const float floor      = 0.8f * std::pow (0.1f, s);   // 0.8 .. 0.25 .. 0.08 level units per frame

    // Threshold comes from the history *before* this frame, so an onset can't raise its own bar.
    // The recent-peak term stops the tail of one hit re-triggering as another.
    lastThreshold = std::max (median() * multiplier + floor, recentPeak * peakHoldRatio);
    recentPeak = std::max (recentPeak * peakDecay, flux);

    history[(size_t) historyWrite] = flux;
    historyWrite = (historyWrite + 1) % historyLength;
    historyCount = std::min (historyCount + 1, historyLength);

    secondsSinceHit = std::min (secondsSinceHit + frameSeconds, 1000.0f);

    if (flux > lastThreshold && secondsSinceHit >= minRetriggerSeconds)
    {
        secondsSinceHit = 0.0f;
        lastStrength = std::clamp (flux / (2.0f * lastThreshold), 0.5f, 1.0f);
        return true;
    }

    return false;
}

} // namespace cortex
