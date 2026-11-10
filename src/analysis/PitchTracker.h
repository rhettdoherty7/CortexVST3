#pragma once

#include <vector>

namespace cortex
{

/** Monophonic pitch tracker (YIN) for bass / vocal / synth tracks, 30 Hz..1 kHz.
    Runs on a copy of the signal decimated to about 12 kHz so it stays cheap on the audio thread.
    No allocation after prepare(). */
class PitchTracker
{
public:
    void prepare (double sampleRate);
    void reset();

    void pushSample (float mono) noexcept
    {
        accumulator += mono;
        if (++accumulatorCount == decimation)
        {
            ring[(size_t) writeIndex] = accumulator / (float) decimation;
            writeIndex = (writeIndex + 1) & ringMask;
            accumulator = 0.0f;
            accumulatorCount = 0;
        }
    }

    /** Analyses the most recent window. hz is 0 when nothing pitched is found. */
    void analyse (float& hz, float& confidence) noexcept;

private:
    std::vector<float> ring, window, difference;
    double decimatedRate = 12000.0;
    int decimation = 4, ringMask = 0, writeIndex = 0;
    int integrationLength = 480, tauMin = 12, tauMax = 400;
    float accumulator = 0.0f;
    int accumulatorCount = 0;
};

} // namespace cortex
