#include "FlashLimiter.h"

#include <algorithm>
#include <cmath>

namespace cortex
{

namespace
{
    constexpr int   areaCells       = FlashLimiter::numCells / 4;   // a quarter of the picture
    constexpr float holdMargin      = 0.9f;    // hold at 90% of the threshold, safely short of a flash
    constexpr double coincidence    = 0.05;    // cells rising within 50 ms of each other form one flash
    constexpr float releaseSeconds  = 0.25f;
}

void FlashLimiter::reset() noexcept
{
    luminanceTracker = {};
    redTracker = {};
    now = 0.0;
    smoothedGain = smoothedRedGain = 1.0f;
}

int FlashLimiter::Tracker::recentFlashes (double t) const noexcept
{
    int count = 0;
    for (double stamp : flashTimes)
        count += (stamp > t - 1.0 && stamp > 0.0) ? 1 : 0;
    return count;
}

float FlashLimiter::Tracker::requiredGain (const float* values, float threshold) const noexcept
{
    // For every cell on the dark side, the gain that keeps it just under a counted rise.
    std::array<float, numCells> needed;
    for (int i = 0; i < numCells; ++i)
    {
        const auto& cell = cells[(size_t) i];
        const float cap = cell.extreme + threshold * holdMargin;
        needed[(size_t) i] = (! cell.high && values[i] > cap) ? cap / values[i] : 1.0f;
    }

    // A flash needs a quarter of the cells, so let the (areaCells - 1) worst ones through.
    std::nth_element (needed.begin(), needed.begin() + (areaCells - 1), needed.end());
    return needed[(size_t) areaCells - 1];
}

void FlashLimiter::Tracker::observe (const float* values, float gain, float threshold, double t) noexcept
{
    int risenTogether = 0;

    for (int i = 0; i < numCells; ++i)
    {
        auto& cell = cells[(size_t) i];
        const float v = values[i] * gain;

        if (! primed)
        {
            cell = { false, true, v, -10.0 };
            continue;
        }

        if (! cell.high)
        {
            cell.extreme = std::min (cell.extreme, v);
            if (v - cell.extreme >= threshold)
            {
                cell.high = true;
                cell.counted = false;
                cell.extreme = v;
                cell.riseTime = t;
            }
        }
        else
        {
            cell.extreme = std::max (cell.extreme, v);
            if (cell.extreme - v >= threshold)
            {
                cell.high = false;
                cell.extreme = v;
            }
        }

        risenTogether += (! cell.counted && t - cell.riseTime <= coincidence) ? 1 : 0;
    }

    primed = true;

    // Each cell's rise counts toward at most one flash, so a fast strobe is counted in full.
    if (risenTogether >= areaCells)
    {
        for (auto& cell : cells)
            if (! cell.counted && t - cell.riseTime <= coincidence)
                cell.counted = true;

        flashTimes[(size_t) flashWrite] = t;
        flashWrite = (flashWrite + 1) % (int) flashTimes.size();
    }
}

FlashLimiter::Result FlashLimiter::process (const float* luminance, const float* redness, float dt, Mode mode) noexcept
{
    now += (double) std::max (dt, 0.0f);

    if (mode == Mode::off)
    {
        // Keep watching so switching the limiter back on starts from a true history.
        luminanceTracker.observe (luminance, 1.0f, 0.1f, now);
        redTracker.observe (redness, 1.0f, 0.1f, now);
        smoothedGain = smoothedRedGain = 1.0f;
        return {};
    }

    // WCAG: a general flash is a 10% change in relative luminance; a red flash is a change of
    // 20 in (R - G - B) x 320, i.e. 0.0625 on a 0..1 scale.
    const int   allowed        = mode == Mode::strict ? 2 : 3;
    const float threshold      = mode == Mode::strict ? 0.08f : 0.1f;
    const float redThreshold   = mode == Mode::strict ? 0.05f : 0.0625f;
    const float release        = 1.0f - std::exp (-dt / releaseSeconds);

    const auto limit = [&] (Tracker& tracker, const float* values, float& smoothed, float threshold)
    {
        // Out of allowance: hold every rise under the threshold. The hold applies instantly;
        // letting go is eased so the picture does not jump back up.
        const float target = tracker.recentFlashes (now) >= allowed ? tracker.requiredGain (values, threshold) : 1.0f;
        smoothed = target < smoothed ? target : smoothed + (target - smoothed) * release;

        tracker.observe (values, smoothed, threshold, now);
        return smoothed;
    };

    Result result;
    result.gain = limit (luminanceTracker, luminance, smoothedGain, threshold);
    result.redDesaturation = 1.0f - limit (redTracker, redness, smoothedRedGain, redThreshold);
    return result;
}

int FlashLimiter::flashesInLastSecond() const noexcept
{
    return luminanceTracker.recentFlashes (now);
}

} // namespace cortex
