// Spectrum Bars. uP[0] layout (0 linear, 1 mirrored, 2 radial), uP[1] bar count, uP[2] gap.

float sdRoundBox(vec2 p, vec2 halfSize, float radius)
{
    vec2 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float level(float f)
{
    // Highs carry less energy than lows, so tilt the display to keep the whole row alive.
    float m = spectrumAt(mix(0.04, 0.93, f));
    return pow(m, 1.6) * (0.8 + 0.55 * f);
}

void main()
{
    float aspect = uRes.x / uRes.y;
    int   barMode = int(uP[0] + 0.5);
    float count  = floor(mix(24.0, 96.0, uP[1]));
    float gap    = mix(0.12, 0.6, uP[2]);
    float punch  = 1.0 + 0.10 * uKick;

    vec2  q;                 // pixels: x across the bar, y along it from its base
    float f, barPitch, len;  // spectrum position 0..1, bar spacing in pixels, bar length in pixels
    float mask = 1.0, reflection = 1.0;

    if (barMode == 2)
    {
        vec2  p  = (vUv - 0.5) * vec2(aspect, 1.0);
        float r  = length(p);
        float xn = abs(atan(p.x, -p.y)) / PI;          // 0 at the bottom, 1 at the top, mirrored
        float r0 = 0.17 * uScale * punch;
        f        = (floor(xn * count) + 0.5) / count;
        barPitch = PI * r0 * uRes.y / count;
        q        = vec2((fract(xn * count) - 0.5) * PI * r * uRes.y / count, (r - r0) * uRes.y);
        len      = level(f) * 0.30 * uScale * uRes.y * punch;
    }
    else
    {
        const float span = 0.86, baseline = 0.36;
        float xn = barMode == 1 ? abs(vUv.x - 0.5) * 2.0 / span : (vUv.x - 0.5 * (1.0 - span)) / span;
        mask     = step(0.0, xn) * step(xn, 1.0);
        xn       = clamp(xn, 0.0, 0.9999);
        f        = (floor(xn * count) + 0.5) / count;
        barPitch = span * uRes.x / count * (barMode == 1 ? 0.5 : 1.0);
        q        = vec2((fract(xn * count) - 0.5) * barPitch, (vUv.y - baseline) * uRes.y);
        len      = level(f) * 0.5 * uScale * uRes.y * punch;

        if (q.y < 0.0)   // floor reflection: squashed, dim, fading with depth
        {
            reflection = 0.30 * exp(q.y / (0.10 * uRes.y));
            q.y = -q.y * 2.4;
        }
    }

    float halfWidth = barPitch * 0.5 * (1.0 - gap);
    len = max(len, 2.0 * halfWidth);                    // a silent bar rests as a dot

    float d        = sdRoundBox(vec2(q.x, q.y - len * 0.5), vec2(halfWidth, len * 0.5), halfWidth);
    float coverage = clamp(0.5 - d, 0.0, 1.0);          // analytic anti-aliasing
    float along    = clamp(q.y / len, 0.0, 1.0);
    float energy   = level(f);

    vec3  hue  = palette(f);
    vec3  body = hue * mix(0.5, 2.8, along * along) * (0.7 + 2.6 * energy);
    float halo = exp(-max(d, 0.0) / (halfWidth * 1.6 + 2.0)) * 0.22 * energy;

    vec3 colour = (body * coverage + hue * halo * 1.4) * reflection * mask * uIntensity;
    fragColor = vec4(colour, clamp((coverage + halo * 0.5) * reflection * mask, 0.0, 1.0));
}
