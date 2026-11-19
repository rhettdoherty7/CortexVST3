// Draws waveforms as chains of screen-space capsules: one instanced quad per segment, with the
// endpoints fetched from the wave textures. Used by Scope (time and X/Y) and Waveform Stack.

uniform sampler2D uWave;          // 256 x 2: row 0 left, row 1 right
uniform sampler2D uWaveHistory;   // 256 x 64: row 0 newest
uniform vec2  uRes;
uniform int   uMode;              // 0 scope over time, 1 scope X/Y, 2 stacked history
uniform float uThickness, uGlow;  // pixels
uniform float uAmp, uLayers, uHistoryPhase, uKick;

out vec2  vUv;
out vec2  vLocal;     // x along the segment, y across it, in pixels
out float vLen;
out float vTone;      // palette position
out float vFade;

const int   SEGMENTS = 255;
const float PI = 3.14159265359;

vec2 pointAt(int i, int layer, out float depth)
{
    float t = float(i) / 255.0;
    depth = 0.0;

    if (uMode == 0)
    {
        float s = texelFetch(uWave, ivec2(i, layer), 0).r;
        return vec2(mix(0.05, 0.95, t), 0.5 + s * 0.36 * uAmp) * uRes;
    }

    if (uMode == 1)
    {
        // Vectorscope: mono content draws a vertical line, wide content spreads sideways.
        float l = texelFetch(uWave, ivec2(i, 0), 0).r;
        float r = texelFetch(uWave, ivec2(i, 1), 0).r;
        return 0.5 * uRes + vec2(l - r, l + r) * 0.5 * uAmp * uRes.y * 0.62;
    }

    depth = (float(layer) + uHistoryPhase) / uLayers;        // 0 nearest, 1 farthest
    float s      = texelFetch(uWaveHistory, ivec2(i, layer), 0).r;
    float window = sin(t * PI);
    float narrow = mix(1.0, 0.42, depth);
    float amp    = mix(0.22, 0.05, depth) * uAmp * (1.0 + 0.6 * uKick * (1.0 - depth));
    return vec2(0.5 + (t - 0.5) * 0.92 * narrow,
                mix(0.12, 0.80, pow(depth, 0.75)) + s * window * window * amp) * uRes;
}

void main()
{
    int seg   = gl_InstanceID % SEGMENTS;
    int layer = gl_InstanceID / SEGMENTS;

    float depth, unused;
    vec2 a = pointAt(seg, layer, depth);
    vec2 b = pointAt(seg + 1, layer, unused);

    vec2  d   = b - a;
    float len = length(d);
    vec2  dir = len > 1.0e-4 ? d / len : vec2(1.0, 0.0);
    vec2  nrm = vec2(-dir.y, dir.x);

    float reach  = uThickness + uGlow;
    vec2  corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    float along  = mix(-reach, len + reach, corner.x);
    float across = mix(-reach, reach, corner.y);
    vec2  pos    = a + dir * along + nrm * across;

    gl_Position = vec4(pos / uRes * 2.0 - 1.0, 0.0, 1.0);
    vUv    = pos / uRes;
    vLocal = vec2(along, across);
    vLen   = len;

    float t = (float(seg) + corner.x) / 255.0;

    if (uMode == 2)
    {
        // Near layers are bright and warm; the newest fades in and the oldest fades out.
        vTone = 1.0 - depth * 0.85;
        vFade = mix(1.0, 0.10, depth)
              * (layer == 0 ? uHistoryPhase : 1.0)
              * (layer == int(uLayers) - 1 ? 1.0 - uHistoryPhase : 1.0);
    }
    else if (uMode == 1)
    {
        vTone = 0.35 + 0.6 * t;
        vFade = clamp(7.0 / (len + 1.0), 0.15, 1.6);          // a slower beam burns brighter
    }
    else
    {
        vTone = layer == 0 ? 0.85 : 0.45;
        vFade = layer == 0 ? 1.0 : 0.7;
    }
}
