#pragma once

#include <array>
#include <cstdint>

namespace cortex
{

struct Rgb { float r = 0.0f, g = 0.0f, b = 0.0f; };

/** sRGB (0..1, gamma-encoded) <-> linear light. */
float srgbToLinear (float encoded) noexcept;
float linearToSrgb (float linear) noexcept;
Rgb   srgbToLinear (Rgb encoded) noexcept;

/** Linear sRGB <-> OKLab. Gradients are mixed in OKLab so midpoints stay clean (North Star Q1). */
Rgb linearToOklab (Rgb linear) noexcept;
Rgb oklabToLinear (Rgb lab) noexcept;
Rgb mixOklab (Rgb linearA, Rgb linearB, float t) noexcept;

/** Five colours ordered dark-to-bright plus a background, defined in sRGB hex (section 5.6). */
struct Palette
{
    const char* name = "";
    std::array<uint32_t, 6> srgbHex {};   // [0..4] colours, [5] background

    Rgb linear (int index) const noexcept;
    Rgb oklab (int index) const noexcept;
};

/** Factory palettes. All original to Cortex. */
int numFactoryPalettes() noexcept;
const Palette& factoryPalette (int index) noexcept;

} // namespace cortex
