// Pulse Grid: a field of glyphs that lights up with the spectrum. Kick raises the density,
// hats reshuffle the characters. uP[0] rows, uP[1] base density.

uniform sampler2D uGlyphs;      // atlas, one glyph per cell, row 0 at the top
uniform float uGlyphCount;
uniform vec2  uGlyphGrid;       // columns, rows in the atlas

void main()
{
    float aspect = uRes.x / uRes.y;
    float rows   = floor(mix(14.0, 44.0, uP[0]) / max(uScale, 0.1));
    vec2  cells  = vec2(floor(rows * aspect / 0.62), rows);   // glyph cells are 0.62 wide
    vec2  g      = vUv * cells;
    vec2  id     = floor(g);
    vec2  local  = fract(g);

    float column = (id.x + 0.5) / cells.x;
    float energy = pow(spectrumAt(mix(0.04, 0.93, column)), 1.4) * (0.8 + 0.5 * column);

    // A slow drifting noise field decides which cells are lit; louder music and kicks light more.
    float field   = vnoise(id * vec2(0.11, 0.17) + vec2(0.0, -uMotion * 0.55));
    float density = mix(0.12, 0.40, uP[1]) + 0.18 * uKick + 0.10 * uRms;
    float lit     = smoothstep(1.0 - density, 1.0 - density + 0.10, field * 0.85 + energy * 0.25);

    // Each cell holds its glyph until a hat lands or its own slow timer ticks over.
    float tick  = floor(uTime * 1.5 + hash21(id) * 7.0);
    float glyph = floor(hash21(id + vec2(tick, uHatCount) * 0.371) * uGlyphCount);
    vec2  cell  = vec2(mod(glyph, uGlyphGrid.x), floor(glyph / uGlyphGrid.x));

    vec2  inset = (local - 0.5) * 1.12 + 0.5;                 // a little air between glyphs
    float inside = step(0.0, inset.x) * step(inset.x, 1.0) * step(0.0, inset.y) * step(inset.y, 1.0);
    vec2  atlasUv = (cell + vec2(inset.x, 1.0 - inset.y)) / uGlyphGrid;
    float ink = texture(uGlyphs, vec2(atlasUv.x, atlasUv.y)).r * inside;

    // A few cells run white-hot for a moment; they move every eighth of a second.
    float spark = step(0.988, hash21(id + floor(uTime * 8.0) * 1.37)) * (0.4 + 0.6 * uHat);

    vec3  hue   = palette(0.25 + 0.75 * field);
    float glow  = 0.035 + lit * (0.35 + 2.4 * energy) + spark * 5.0;   // unlit glyphs stay faintly visible
    vec3  colour = (hue * glow + palette(1.0) * spark * 2.0) * ink * uIntensity;

    fragColor = vec4(colour, clamp(ink * (0.035 + lit + spark), 0.0, 1.0));
}
