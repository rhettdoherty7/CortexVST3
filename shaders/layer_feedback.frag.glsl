// Feedback / Echo trails: the last frame, zoomed and turned a little, fading under the new one.
// uL[0] zoom rate, uL[1] rotation rate (0.5 = none), uL[2] trail length. All rates are per second,
// so trails look the same at any frame rate.

uniform sampler2D uSource;
uniform sampler2D uHistory;
uniform float uAmount;
uniform float uL[4];

void main()
{
    vec3  c      = texture(uSource, vUv).rgb;
    float aspect = uRes.x / uRes.y;

    vec2  p     = (vUv - 0.5) * vec2(aspect, 1.0);
    float zoom  = 1.0 - mix(0.0, 0.35, uL[0]) * (1.0 + uKick) * uDt;
    float angle = (uL[1] - 0.5) * 0.5 * uDt;
    p = mat2(cos(angle), -sin(angle), sin(angle), cos(angle)) * p * zoom;
    vec3 echo = texture(uHistory, p / vec2(aspect, 1.0) + 0.5).rgb;

    // Decay per frame, from a trail time constant (frame-rate independent).
    float trail = mix(0.04, 0.35, uL[2]) * uAmount;
    float keep  = trail > 0.0 ? exp(-uDt / trail) : 0.0;

    // Echoes are dimmer than the original and never add up past it, so the frame cannot wash out.
    fragColor = vec4(max(c, echo * keep * 0.85), 1.0);
}
