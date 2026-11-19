// One point per particle; the position comes from the simulation texture.

uniform sampler2D uState;
uniform ivec2 uStateSize;
uniform float uAspect, uPointSize;

out vec2  vUv;
out float vLife;
out float vSeed;

void main()
{
    ivec2 texel = ivec2(gl_VertexID % uStateSize.x, gl_VertexID / uStateSize.x);
    vec4  state = texelFetch(uState, texel, 0);

    vUv   = vec2(state.x / uAspect, state.y);
    vLife = state.z;
    vSeed = state.w;

    gl_Position  = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = uPointSize * (0.6 + 0.9 * state.w);
}
