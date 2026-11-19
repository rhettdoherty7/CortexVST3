#pragma once

#include <array>

namespace cortex
{

/** Adaptive-threshold onset detector for one flux stream (section 4.2).
    Fires when the flux exceeds  median(last ~0.5 s) x multiplier + floor,  and 30% of the
    recent peak (150 ms decay), with a minimum retrigger time. Sensitivity 0..1 lowers both the multiplier and the floor. */
class OnsetDetector
{
public:
    void prepare (double frameSeconds);
    void reset();

    /** Feed one frame's flux. Returns true on the frame an onset is detected. */
    bool process (float flux, float sensitivity, float minRetriggerSeconds) noexcept;

    /** 0.5..1: how far the last hit cleared its threshold. Valid on the frame process() returned true. */
    float strength() const noexcept   { return lastStrength; }
    float threshold() const noexcept  { return lastThreshold; }

private:
    static constexpr int maxHistory = 64;

    float median() noexcept;

    std::array<float, maxHistory> history {};
    std::array<float, maxHistory> scratch {};
    int   historyLength = 48, historyCount = 0, historyWrite = 0;
    float frameSeconds = 0.01f;
    float secondsSinceHit = 1000.0f;
    float lastStrength = 0.0f, lastThreshold = 0.0f;
    float recentPeak = 0.0f, peakDecay = 0.93f;
    static constexpr float peakHoldRatio = 0.3f;
};

} // namespace cortex
