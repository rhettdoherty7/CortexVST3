// Bloom Orb: a glowing core with a corona, drifting rays, a snare flare and a kick shockwave.
// uP[0] ray amount, uP[1] flare amount.

void main()
{
    float aspect = uRes.x / uRes.y;
    vec2  p   = (vUv - 0.5) * vec2(aspect, 1.0) / max(uScale, 0.05);
    float r   = length(p);
    vec2  dir = p / max(r, 1.0e-4);
    float t   = uMotion;

    float size = 0.105 + 0.16 * uRms + 0.035 * uKick;

    // Core: a soft-edged disc whose rim wobbles with slow noise.
    float surface = fbm(p * 6.0 + vec2(t * 0.25, -t * 0.18));
    float core    = smoothstep(size, size * 0.55, r + (surface - 0.5) * size * 0.35);
    float depth   = clamp(1.0 - r / size, 0.0, 1.0);
    vec3  coreCol = mix(palette(0.55), palette(1.0), smoothstep(0.2, 0.9, surface * 0.6 + depth * 0.6))
                  * (2.0 + 5.0 * depth);

    // Corona: inverse-power falloff around the core.
    float corona = pow(size / max(r, size * 0.6), 2.8);

    // Rays: two noise fields around the circle (sampled on the direction, so there is no seam).
    float rayA = fbm(dir * 2.2 + vec2(t * 0.07, 3.1));
    float rayB = fbm(dir * 5.5 - vec2(t * 0.05, 1.7));
    float rays = pow(rayA * rayB * 2.2, 2.0);
    // Rays stay close to the core so the frame keeps its dark negative space.
    float reach = 0.25 + 0.9 * uBands[1] + 0.5 * uKick;
    float rayFalloff = exp(-max(r - size, 0.0) / (0.03 + 0.06 * reach));

    // Snare: an anamorphic streak, wide and thin, with a short vertical partner.
    float streak  = uSnare * exp(-abs(p.y) * 90.0)  / (1.0 + pow(abs(p.x) * 2.2, 2.0));
    float streakV = uSnare * exp(-abs(p.x) * 160.0) / (1.0 + pow(abs(p.y) * 10.0, 2.0));

    // Kick: a ring that leaves the core and fades as it travels.
    float ringRadius = size * 1.1 + uSinceKick * 0.85;
    float ring = exp(-pow((r - ringRadius) * 38.0, 2.0)) * exp(-uSinceKick * 5.0);

    vec3 colour = coreCol * core;
    colour += palette(0.45) * corona * 0.4 * (1.0 - core);
    colour += palette(0.8)  * rays * rayFalloff * 0.9 * (1.0 - core) * mix(0.3, 1.4, uP[0]);
    colour += palette(1.0)  * (streak * 5.0 + streakV * 1.5) * mix(0.2, 1.6, uP[1]);
    colour += palette(0.7)  * ring * 2.0;

    fragColor = emit(colour * uIntensity);
}
