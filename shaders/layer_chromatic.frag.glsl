// Chromatic Aberration: red and blue pulled apart from the centre outward; kicks push it further.
// uL[0] kick response.

uniform sampler2D uSource;
uniform float uAmount;
uniform float uL[4];

void main()
{
    vec2  fromCentre = vUv - 0.5;
    float strength   = uAmount * (0.004 + 0.012 * uL[0] * uKick);
    vec2  offset     = fromCentre * strength * (0.5 + 2.0 * dot(fromCentre, fromCentre));

    float r = texture(uSource, vUv + offset).r;
    float g = texture(uSource, vUv).g;
    float b = texture(uSource, vUv - offset).b;
    fragColor = vec4(r, g, b, 1.0);
}
