// Scanlines / CRT: barrel curve, scanlines, an aperture-grille mask and dark corners.
// uL[0] line density, uL[1] curvature.

uniform sampler2D uSource;
uniform float uAmount;
uniform float uL[4];

void main()
{
    vec2  p     = vUv * 2.0 - 1.0;
    float bend  = mix(0.0, 0.12, uL[1]) * uAmount;
    p *= 1.0 + bend * dot(p, p);
    vec2  uv    = p * 0.5 + 0.5;
    float inside = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);

    vec3  c     = texture(uSource, clamp(uv, 0.0, 1.0)).rgb;
    float lines = mix(uRes.y * 0.25, uRes.y * 0.5, uL[0]);
    float scan  = 0.5 + 0.5 * cos(uv.y * lines * TAU);
    int   triad = int(mod(gl_FragCoord.x, 3.0));
    vec3  mask  = triad == 0 ? vec3(1.0, 0.75, 0.75) : (triad == 1 ? vec3(0.75, 1.0, 0.75) : vec3(0.75, 0.75, 1.0));

    // Bright pixels bloom over the dark gap between lines, as on a real tube.
    float l = luma(c);
    vec3  crt = c * mask * mix(0.55, 1.0, max(scan, clamp(l * 0.6, 0.0, 1.0))) * 1.25;
    fragColor = vec4(mix(c, crt, uAmount) * inside, 1.0);
}
