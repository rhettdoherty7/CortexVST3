// Vignette. uL[0] size, uL[1] softness.

uniform sampler2D uSource;
uniform float uAmount;
uniform float uL[4];

void main()
{
    vec3  c      = texture(uSource, vUv).rgb;
    vec2  p      = (vUv - 0.5) * vec2(uRes.x / uRes.y, 1.0);
    float inner  = mix(0.25, 0.65, uL[0]);
    float outer  = inner + mix(0.25, 0.9, uL[1]);
    float shade  = smoothstep(outer, inner, length(p));
    fragColor = vec4(c * mix(1.0, shade, uAmount), 1.0);
}
