// Soft round particle, added into the trail buffer.

uniform float uBrightness;

in float vLife;
in float vSeed;

void main()
{
    float falloff = smoothstep(0.5, 0.0, length(gl_PointCoord - 0.5));
    float fade    = smoothstep(0.0, 0.12, vLife) * smoothstep(1.0, 0.88, vLife);   // ease in and out
    float sparkle = 1.0 + 2.5 * uHat * step(0.92, vSeed);

    // A slow density field carves bright currents and dark voids, so the frame has structure.
    vec2  p = vec2(vUv.x * uRes.x / uRes.y, vUv.y);
    float density = smoothstep(0.40, 0.72, fbm(p * 1.4 + vec2(uMotion * 0.03, -uMotion * 0.02)));

    vec3 colour = palette(0.2 + 0.8 * vSeed) * falloff * fade * sparkle * mix(0.06, 1.0, density)
                * uBrightness * uIntensity;
    fragColor = vec4(colour, 0.0);
}
