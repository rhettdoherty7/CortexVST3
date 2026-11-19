// Bloom upsample: 3x3 tent filter, added onto the next larger level.

uniform sampler2D uSource;
uniform vec2 uTexel;      // one texel of the (smaller) source

vec3 tap(vec2 offset) { return texture(uSource, vUv + offset * uTexel).rgb; }

void main()
{
    vec3 colour = tap(vec2(0.0)) * 4.0
                + (tap(vec2(-1.0, 0.0)) + tap(vec2(1.0, 0.0)) + tap(vec2(0.0, -1.0)) + tap(vec2(0.0, 1.0))) * 2.0
                + (tap(vec2(-1.0, -1.0)) + tap(vec2(1.0, -1.0)) + tap(vec2(-1.0, 1.0)) + tap(vec2(1.0, 1.0)));

    fragColor = vec4(colour / 16.0, 1.0);
}
