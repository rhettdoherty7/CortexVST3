#include "Palette.h"

#include <algorithm>
#include <cmath>

namespace cortex
{

float srgbToLinear (float c) noexcept
{
    return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb (float c) noexcept
{
    c = std::clamp (c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow (c, 1.0f / 2.4f) - 0.055f;
}

Rgb srgbToLinear (Rgb c) noexcept
{
    return { srgbToLinear (c.r), srgbToLinear (c.g), srgbToLinear (c.b) };
}

// OKLab, Bjorn Ottosson 2020. Rgb is reused to carry (L, a, b).
Rgb linearToOklab (Rgb c) noexcept
{
    const float l = std::cbrt (0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b);
    const float m = std::cbrt (0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b);
    const float s = std::cbrt (0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b);

    return { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
             1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
             0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
}

Rgb oklabToLinear (Rgb lab) noexcept
{
    const float l_ = lab.r + 0.3963377774f * lab.g + 0.2158037573f * lab.b;
    const float m_ = lab.r - 0.1055613458f * lab.g - 0.0638541728f * lab.b;
    const float s_ = lab.r - 0.0894841775f * lab.g - 1.2914855480f * lab.b;
    const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;

    return {  4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
             -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
             -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s };
}

Rgb mixOklab (Rgb a, Rgb b, float t) noexcept
{
    const Rgb la = linearToOklab (a), lb = linearToOklab (b);
    return oklabToLinear ({ la.r + (lb.r - la.r) * t, la.g + (lb.g - la.g) * t, la.b + (lb.b - la.b) * t });
}

Rgb Palette::linear (int index) const noexcept
{
    const uint32_t hex = srgbHex[(size_t) std::clamp (index, 0, 5)];
    return srgbToLinear (Rgb { (float) ((hex >> 16) & 0xff) / 255.0f,
                               (float) ((hex >> 8) & 0xff) / 255.0f,
                               (float) (hex & 0xff) / 255.0f });
}

Rgb Palette::oklab (int index) const noexcept
{
    return linearToOklab (linear (index));
}

namespace
{
    // Colours run dark to bright; the last entry is the background.
    constexpr Palette palettes[] = {
        { "Cortex",       { 0x2A1B6B, 0x5B3FD9, 0x7C5CFF, 0xB79CFF, 0xF2ECFF, 0x06060A } },
        { "Night Drive",  { 0x1B1F5E, 0x3D4FD6, 0xFF4FA3, 0x00C2FF, 0xFFE3F1, 0x05060C } },
        { "Ember",        { 0x5A0F1E, 0xB3261E, 0xFF5A1F, 0xFFA63D, 0xFFF1C9, 0x0A0606 } },
        { "Glacier",      { 0x0B2E4F, 0x1466A8, 0x2FB6E8, 0x8DE8F5, 0xF0FDFF, 0x04080C } },
        { "Acid",         { 0x143B1A, 0x1F8F3A, 0x6BE04A, 0xD4FF4F, 0xFBFFD6, 0x050805 } },
        { "Rose Gold",    { 0x4A1F33, 0x9C3F5F, 0xE0738A, 0xF5B7A1, 0xFFF0E3, 0x0A0607 } },
        { "Deep Sea",     { 0x06283D, 0x0B5563, 0x12A594, 0x6FE3C1, 0xE8FFF6, 0x030809 } },
        { "Ultraviolet",  { 0x2B0A4F, 0x6A1FB8, 0xB03CFF, 0xFF6BE6, 0xFFE6FB, 0x07040B } },
        { "Sodium",       { 0x4A2A05, 0xA85F0A, 0xF59E0B, 0xFFD166, 0xFFF6DC, 0x090705 } },
        { "Mono Ice",     { 0x20242C, 0x4A5361, 0x8894A6, 0xC9D3E0, 0xFFFFFF, 0x050607 } },
        { "Infrared",     { 0x3B0A2A, 0x99103F, 0xFF2E4D, 0xFF8A3D, 0xFFE9B8, 0x0A0406 } },
        { "Terminal",     { 0x06301A, 0x0B6B34, 0x1FD15E, 0x8CFFB0, 0xEFFFF3, 0x030705 } },
    };
}

int numFactoryPalettes() noexcept
{
    return (int) (sizeof (palettes) / sizeof (palettes[0]));
}

const Palette& factoryPalette (int index) noexcept
{
    return palettes[(size_t) std::clamp (index, 0, numFactoryPalettes() - 1)];
}

} // namespace cortex
