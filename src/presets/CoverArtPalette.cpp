#include "CoverArtPalette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cortex
{

namespace
{
    constexpr int clusters = 6;

    float distanceSquared (const Rgb& a, const Rgb& b) noexcept
    {
        const float dl = a.r - b.r, da = a.g - b.g, db = a.b - b.b;
        return dl * dl + da * da + db * db;
    }

    uint32_t toHex (Rgb lab) noexcept
    {
        const Rgb linear = oklabToLinear (lab);
        const auto channel = [] (float v) { return (uint32_t) std::lround (linearToSrgb (std::clamp (v, 0.0f, 1.0f)) * 255.0f); };
        return (channel (linear.r) << 16) | (channel (linear.g) << 8) | channel (linear.b);
    }
}

Palette paletteFromPixels (const std::vector<Rgb>& pixels)
{
    Palette result;
    result.name = "Cover Art";

    std::vector<Rgb> lab;
    lab.reserve (pixels.size());
    for (const auto& p : pixels)
        lab.push_back (linearToOklab (srgbToLinear (p)));

    if (lab.empty())
        return factoryPalette (0);

    // k-means++ seeding from a fixed seed, so results are repeatable.
    std::array<Rgb, clusters> centres {};
    uint32_t seed = 0x51ed2701u;
    const auto random01 = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) / 16777216.0f;
    };

    centres[0] = lab[(size_t) (random01() * (float) (lab.size() - 1))];
    std::vector<float> nearest (lab.size(), 0.0f);
    for (int c = 1; c < clusters; ++c)
    {
        double total = 0.0;
        for (size_t i = 0; i < lab.size(); ++i)
        {
            float best = 1.0e9f;
            for (int k = 0; k < c; ++k)
                best = std::min (best, distanceSquared (lab[i], centres[(size_t) k]));
            nearest[i] = best;
            total += best;
        }

        size_t pick = 0;
        if (total > 0.0)
        {
            double target = random01() * total;
            for (; pick + 1 < lab.size(); ++pick)
            {
                target -= nearest[pick];
                if (target <= 0.0)
                    break;
            }
        }
        centres[(size_t) c] = lab[pick];
    }

    // Lloyd iterations.
    std::vector<int> assignment (lab.size(), 0);
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        std::array<double, clusters * 3> sums {};
        std::array<int, clusters> counts {};

        for (size_t i = 0; i < lab.size(); ++i)
        {
            int best = 0;
            for (int k = 1; k < clusters; ++k)
                if (distanceSquared (lab[i], centres[(size_t) k]) < distanceSquared (lab[i], centres[(size_t) best]))
                    best = k;
            assignment[i] = best;
            sums[(size_t) best * 3] += lab[i].r;
            sums[(size_t) best * 3 + 1] += lab[i].g;
            sums[(size_t) best * 3 + 2] += lab[i].b;
            ++counts[(size_t) best];
        }

        for (int k = 0; k < clusters; ++k)
            if (counts[(size_t) k] > 0)   // an empty cluster keeps its old centre
                centres[(size_t) k] = { (float) (sums[(size_t) k * 3] / counts[(size_t) k]),
                                        (float) (sums[(size_t) k * 3 + 1] / counts[(size_t) k]),
                                        (float) (sums[(size_t) k * 3 + 2] / counts[(size_t) k]) };
    }

    std::sort (centres.begin(), centres.end(), [] (const Rgb& a, const Rgb& b) { return a.r < b.r; });

    // Background: the darkest cluster, pushed to near black and calmed down.
    Rgb background = centres[0];
    background.r = std::min (background.r * 0.35f, 0.14f);
    background.g *= 0.5f;
    background.b *= 0.5f;
    result.srgbHex[5] = toHex (background);

    // Colours: spread lightness from 0.30 to 0.90, keep each cluster's hue and chroma.
    float previous = 0.0f;
    for (int i = 0; i < 5; ++i)
    {
        Rgb c = centres[(size_t) i + 1];
        const float target = 0.30f + 0.15f * (float) i;
        c.r = std::max (c.r + (target - c.r) * 0.6f, previous + 0.04f);
        previous = c.r;

        // Very light colours cannot hold much chroma; scale it down rather than clip the hue.
        const float maxChroma = 0.32f * (1.0f - std::max (0.0f, c.r - 0.75f) * 3.0f);
        const float chroma = std::sqrt (c.g * c.g + c.b * c.b);
        if (chroma > maxChroma && chroma > 0.0f)
        {
            c.g *= maxChroma / chroma;
            c.b *= maxChroma / chroma;
        }

        result.srgbHex[(size_t) i] = toHex (c);
    }

    return result;
}

} // namespace cortex
