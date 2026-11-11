#pragma once

#include <array>

namespace cortex
{

/** Photosensitivity limiter (section 9), following WCAG 2.3.1's definition of a general flash:
    a rise in relative luminance of 10% of full scale or more, over at least a quarter of the
    picture, followed by a fall. No more than three are let through in any one second.

    The frame is measured on a 4x4 grid. Each call takes the grid for the frame about to be shown
    and returns a gain (<= 1) to apply to it. Once the second's allowance is used up, the gain holds
    the picture just under the level that would count as another flash. Pure CPU, no allocation. */
class FlashLimiter
{
public:
    enum class Mode { off = 0, standard, strict };

    static constexpr int numCells = 16;

    struct Result
    {
        float gain = 1.0f;            // multiply the frame's linear colour by this
        float redDesaturation = 0.0f; // 0..1: mix saturated red toward grey by this much
    };

    void reset() noexcept;

    /** luminance: mean relative luminance (0..1) of each grid cell.
        redness: mean saturated-red amount (0..1) of each grid cell. */
    Result process (const float* luminance, const float* redness, float dtSeconds, Mode mode) noexcept;

    /** Flashes let through in the last second (for the debug readout and the tests). */
    int flashesInLastSecond() const noexcept;

private:
    struct Cell
    {
        bool  high = false;       // currently on the bright side of a transition
        bool  counted = true;     // its last rise is already part of a counted flash
        float extreme = 0.0f;     // lowest value since going low, or highest since going high
        double riseTime = -10.0;  // when this cell last rose by the threshold
    };

    struct Tracker
    {
        std::array<Cell, numCells> cells {};
        std::array<double, 8> flashTimes {};
        int flashWrite = 0;
        bool primed = false;

        int recentFlashes (double now) const noexcept;
        float requiredGain (const float* values, float threshold) const noexcept;
        void observe (const float* values, float gain, float threshold, double now) noexcept;
    };

    Tracker luminanceTracker, redTracker;
    double now = 0.0;
    float smoothedGain = 1.0f, smoothedRedGain = 1.0f;
};

} // namespace cortex
