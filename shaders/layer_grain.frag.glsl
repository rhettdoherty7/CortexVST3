// Film Grain. uL[0] size, uL[1] colour amount. Strongest in the mid-tones, like film.

uniform sampler2D uSource;
uniform float uAmount;
uniform float uL[4];

void main()
{
    vec3  c    = texture(uSource, vUv).rgb;
    float size = mix(1.0, 3.0, uL[0]);
    vec2  cell = floor(gl_FragCoord.xy / size);
    float seed = floor(uTime * 24.0);                          // grain changes at film rate
    vec3  n    = vec3(hash21(cell + seed * 1.37), hash21(cell + seed * 2.11 + 7.0), hash21(cell + seed * 3.07 + 13.0)) - 0.5;
    n = mix(vec3(n.x), n, uL[1]);

    float l      = luma(c);
    float weight = 4.0 * (l / (1.0 + l)) * (1.0 - l / (1.0 + l));   // peaks in the mid-tones
    c *= 1.0 + n * uAmount * 0.6 * (0.25 + weight);
    fragColor = vec4(max(c, vec3(0.0)), 1.0);
}
