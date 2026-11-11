// Gradient Map: replaces colour with the palette by brightness (mixed in OKLab), keeping the
// picture's light. uL[0] contrast.

uniform sampler2D uSource;
uniform float uAmount;
uniform float uL[4];

void main()
{
    vec3  c = texture(uSource, vUv).rgb;
    float l = luma(c);
    float t = pow(l / (l + 0.35), mix(0.6, 1.6, uL[0]));     // compress HDR luminance into 0..1
    vec3  g = palette(t);
    vec3  mapped = g * (l / max(luma(g), 0.02));              // same luminance as the original
    fragColor = vec4(mix(c, mapped, uAmount), 1.0);
}
