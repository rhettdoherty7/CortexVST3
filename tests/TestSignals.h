#pragma once

// Deterministic synthetic drum patterns for the detector tests. Everything is generated in
// memory from a fixed seed, so the tests need no audio files (docs/DECISIONS.md D-004).

#include <cmath>
#include <cstdint>
#include <vector>

namespace testsignals
{

constexpr double pi = 3.14159265358979323846;

struct Pattern
{
    double sampleRate = 48000.0;
    std::vector<float> left, right;
    std::vector<int64_t> kicks, snares, hats;   // onset positions in samples
    std::vector<int64_t> optionalHats;          // hats that land on a snare: detecting them is optional
};

class Noise
{
public:
    explicit Noise (uint32_t seed) : state (seed) {}

    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) (state >> 8) / 8388608.0f - 1.0f;
    }

private:
    uint32_t state;
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    static Biquad highpass (double sampleRate, double hz, double q)
    {
        const double w = 2.0 * pi * hz / sampleRate, c = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        return { (1.0 + c) * 0.5 / a0, -(1.0 + c) / a0, (1.0 + c) * 0.5 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
    }

    static Biquad lowpass (double sampleRate, double hz, double q)
    {
        const double w = 2.0 * pi * hz / sampleRate, c = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        return { (1.0 - c) * 0.5 / a0, (1.0 - c) / a0, (1.0 - c) * 0.5 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
    }

    float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

inline void addInto (Pattern& p, int64_t start, const std::vector<float>& sound, float pan = 0.0f)
{
    const float gainL = 1.0f - 0.3f * (pan > 0.0f ? pan : 0.0f);
    const float gainR = 1.0f + 0.3f * (pan < 0.0f ? pan : 0.0f);

    for (size_t i = 0; i < sound.size(); ++i)
    {
        const auto idx = (size_t) start + i;
        if (idx >= p.left.size())
            break;
        p.left[idx]  += sound[i] * gainL;
        p.right[idx] += sound[i] * gainR;
    }
}

/** Sine sweeping 150 -> 50 Hz with a 120 ms decay. */
inline std::vector<float> makeKick (double sr)
{
    std::vector<float> out ((size_t) (sr * 0.35));
    double phase = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = (double) i / sr;
        phase += 2.0 * pi * (50.0 + 100.0 * std::exp (-t / 0.03)) / sr;
        out[i] = (float) (0.8 * std::exp (-t / 0.12) * std::sin (phase));
    }
    return out;
}

/** 190 Hz body plus 2-6 kHz noise. */
inline std::vector<float> makeSnare (double sr, uint32_t seed)
{
    std::vector<float> out ((size_t) (sr * 0.25));
    Noise noise (seed);
    auto hp = Biquad::highpass (sr, 2000.0, 0.7);
    auto lp = Biquad::lowpass (sr, 6000.0, 0.7);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = (double) i / sr;
        const float body = (float) (0.45 * std::exp (-t / 0.06) * std::sin (2.0 * pi * 190.0 * t));
        const float hiss = lp.process (hp.process (noise.next())) * (float) (0.45 * std::exp (-t / 0.07));
        out[i] = body + hiss;
    }
    return out;
}

/** Short burst of noise above 8 kHz. */
inline std::vector<float> makeHat (double sr, uint32_t seed)
{
    std::vector<float> out ((size_t) (sr * 0.08));
    Noise noise (seed);
    auto hp1 = Biquad::highpass (sr, 8000.0, 0.7);
    auto hp2 = Biquad::highpass (sr, 8000.0, 0.7);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = (double) i / sr;
        out[i] = hp2.process (hp1.process (noise.next())) * (float) (0.35 * std::exp (-t / 0.018));
    }
    return out;
}

/** Builds a pattern from 16th-note step lists ('x' = hit) repeated for `bars` bars, after one beat of silence. */
inline Pattern makePattern (double sr, double bpm, int bars, const char* kickSteps, const char* snareSteps,
                            const char* hatSteps, const std::vector<double>& extraHatBeats = {})
{
    Pattern p;
    p.sampleRate = sr;
    const double samplesPerBeat = sr * 60.0 / bpm;
    const double lead = samplesPerBeat;
    const auto length = (size_t) (lead + samplesPerBeat * 4.0 * bars + sr * 0.5);
    p.left.assign (length, 0.0f);
    p.right.assign (length, 0.0f);

    const auto kick = makeKick (sr);
    uint32_t seed = 1;

    for (int bar = 0; bar < bars; ++bar)
    {
        for (int step = 0; step < 16; ++step)
        {
            const auto pos = (int64_t) (lead + (bar * 4.0 + step * 0.25) * samplesPerBeat);
            const bool isSnare = snareSteps[step] == 'x';

            if (kickSteps[step] == 'x')
            {
                addInto (p, pos, kick);
                p.kicks.push_back (pos);
            }
            if (isSnare)
            {
                addInto (p, pos, makeSnare (sr, seed++), 0.2f);
                p.snares.push_back (pos);
            }
            if (hatSteps[step] == 'x')
            {
                addInto (p, pos, makeHat (sr, seed++), -0.3f);
                (isSnare ? p.optionalHats : p.hats).push_back (pos);
            }
        }

        for (double beat : extraHatBeats)
        {
            const auto pos = (int64_t) (lead + (bar * 4.0 + beat) * samplesPerBeat);
            addInto (p, pos, makeHat (sr, seed++), -0.3f);
            p.hats.push_back (pos);
        }
    }

    return p;
}

inline Pattern fourOnFloor (double sr)
{
    return makePattern (sr, 124.0, 4,
                        "x...x...x...x...",
                        "....x.......x...",
                        "..x...x...x...x.");
}

inline Pattern breakbeat (double sr)
{
    return makePattern (sr, 98.0, 4,
                        "x.........x.....",
                        "....x..x....x...",
                        "x.x.x.x.x.x.x.x.");
}

/** 140 BPM trap: sparse kicks, clap on 3, 8th-note hats with a 32nd-note roll in the last beat. */
inline Pattern trapHats (double sr)
{
    return makePattern (sr, 140.0, 4,
                        "x.....x...x.....",
                        "........x.......",
                        "x.x.x.x.x.x.x...",
                        { 3.25, 3.375, 3.5, 3.625, 3.75, 3.875 });
}

} // namespace testsignals
