#pragma once

#include <engine/Palette.h>

#include <vector>

namespace cortex
{

/** Builds a palette from cover art (section 5.6): k-means with k = 6 in OKLab, sorted by lightness.
    The darkest cluster becomes the background (darkened to near black so visuals glow against it);
    the other five become the colours, dark to bright, with their lightness spread out a little so
    the gradient has range. Hue and chroma come from the art.

    `pixels` are sRGB-encoded colours, 0..1. Pass a downsampled image: a few thousand pixels is
    plenty. Deterministic: the same pixels always give the same palette. */
Palette paletteFromPixels (const std::vector<Rgb>& pixels);

} // namespace cortex
