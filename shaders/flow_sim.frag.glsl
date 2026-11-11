// Flowfield simulation step. Each texel is one particle: xy = position (x runs 0..aspect),
// z = remaining life 0..1, w = a per-particle random seed.

uniform sampler2D uState;
uniform float uAspect;
uniform float uFieldScale, uFlowSpeed;

// Curl of a noise potential: a divergence-free field, so particles swirl instead of bunching up.
vec2 flow(vec2 p)
{
    const float e = 0.01;
    vec2 q = p * uFieldScale + vec2(0.0, uMotion * 0.05);
    float up = fbm(q + vec2(0.0, e)), down  = fbm(q - vec2(0.0, e));
    float right = fbm(q + vec2(e, 0.0)), left = fbm(q - vec2(e, 0.0));
    return vec2(up - down, left - right) / (2.0 * e);
}

void main()
{
    vec4  state = texture(uState, vUv);
    vec2  p     = state.xy;
    float life  = state.z;
    float seed  = state.w;

    vec2 velocity = flow(p) * (0.035 + 0.10 * uRms + 0.08 * uBands[1]) * uFlowSpeed;

    // Kick: a burst outward from the centre, strongest near it.
    vec2  fromCentre = p - vec2(uAspect * 0.5, 0.5);
    float r = length(fromCentre);
    velocity += fromCentre / max(r, 1.0e-4) * uKick * uKick * 0.55 * exp(-r * 2.0);

    p    += velocity * uDt;
    life -= uDt * (0.10 + 0.25 * seed);

    bool dead = life <= 0.0 || p.x < -0.05 || p.x > uAspect + 0.05 || p.y < -0.05 || p.y > 1.05;
    if (dead)
    {
        vec2 h = hash22(vUv * 917.0 + vec2(uTime * 13.7, seed * 51.0));
        p    = vec2(h.x * uAspect, h.y);
        life = 0.55 + 0.45 * hash21(h * 31.0 + 0.5);
        seed = hash21(vUv * 113.0 + uTime);
    }

    fragColor = vec4(p, life, seed);
}
