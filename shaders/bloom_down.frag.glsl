// Bloom downsample: 13-tap filter (box of boxes). The first pass weights each group by
// 1 / (1 + luma) so single hot pixels cannot turn into flickering blobs.

uniform sampler2D uSource;
uniform vec2 uTexel;      // one texel of the source
uniform int  uFirst;
uniform float uThreshold;   // first pass only: light below this does not bloom (soft knee)

vec3 tap(vec2 offset) { return texture(uSource, vUv + offset * uTexel).rgb; }

vec3 karis(vec3 a, vec3 b, vec3 c, vec3 d)
{
    vec3 avg = (a + b + c + d) * 0.25;
    return avg / (1.0 + luma(avg));
}

void main()
{
    vec3 a = tap(vec2(-2.0,  2.0)), b = tap(vec2(0.0,  2.0)), c = tap(vec2(2.0,  2.0));
    vec3 d = tap(vec2(-2.0,  0.0)), e = tap(vec2(0.0,  0.0)), f = tap(vec2(2.0,  0.0));
    vec3 g = tap(vec2(-2.0, -2.0)), h = tap(vec2(0.0, -2.0)), i = tap(vec2(2.0, -2.0));
    vec3 j = tap(vec2(-1.0,  1.0)), k = tap(vec2(1.0,  1.0));
    vec3 l = tap(vec2(-1.0, -1.0)), m = tap(vec2(1.0, -1.0));

    vec3 colour;
    if (uFirst == 1)
    {
        colour = karis(j, k, l, m) * 0.5
               + (karis(a, b, d, e) + karis(b, c, e, f) + karis(d, e, g, h) + karis(e, f, h, i)) * 0.125;
        colour /= max(1.0 - luma(colour), 0.05);   // undo the weighting's average darkening
        float lum = luma(colour);
        float knee = max(uThreshold * 0.5, 1.0e-4);
        float soft = clamp(lum - uThreshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee);
        colour *= max(soft, lum - uThreshold) / max(lum, 1.0e-4);
    }
    else
    {
        colour = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }

    fragColor = vec4(max(colour, vec3(0.0)), 1.0);
}
