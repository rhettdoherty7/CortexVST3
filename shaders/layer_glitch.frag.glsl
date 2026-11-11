// Glitch: horizontal bands slip sideways and split their colour channels, mostly on snares.
// uL[0] block size, uL[1] how much the snare drives it.

uniform sampler2D uSource;
uniform float uAmount;
uniform float uL[4];

void main()
{
    float rows    = mix(48.0, 12.0, uL[0]);
    float band    = floor(vUv.y * rows);
    float tick    = floor(uTime * 15.0);
    float drive   = uAmount * (0.15 + uL[1] * 2.0 * uSnare);
    float chosen  = step(1.0 - 0.35 * drive, hash21(vec2(band, tick)));
    float shift   = (hash21(vec2(band * 3.1, tick + 7.0)) - 0.5) * 0.12 * drive * chosen;
    float split   = 0.006 * drive * chosen;

    vec2 uv = vec2(fract(vUv.x + shift), vUv.y);
    vec3 c  = vec3(texture(uSource, uv + vec2(split, 0.0)).r,
                   texture(uSource, uv).g,
                   texture(uSource, uv - vec2(split, 0.0)).b);
    fragColor = vec4(c, 1.0);
}
