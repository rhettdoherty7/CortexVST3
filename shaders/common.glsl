// Shared uniform contract for every module shader (CORTEX.md section 1.5), plus helpers.
// The loader puts "#version 410 core" and this file in front of each module's fragment shader.

uniform float uTime;          // seconds, scaled by Speed
uniform float uDt;
uniform vec2  uRes;           // size of the target in pixels
uniform float uKick, uSnare, uHat;        // hit envelopes 0..1
uniform float uRms, uFlux, uCentroid;
uniform float uBands[8];
uniform sampler2D uSpectrum;  // 128 x 1, 0..1
uniform sampler2D uWave;      // 256 x 2, row 0 left, row 1 right, -1..1
uniform float uBeatPhase, uBarPhase;
uniform vec3  uPalette[6];    // linear RGB, [0..4] dark to bright, [5] background
uniform float uIntensity, uSpeed, uScale, uColorShift;
uniform float uP[16];         // module parameters uP0..uP15

// Additions to the contract (docs/DECISIONS.md D-014)
uniform vec3  uPaletteLab[6];             // the same palette in OKLab, for clean gradients
uniform float uSinceKick, uSinceSnare, uSinceHat;   // seconds since the last hit
uniform float uKickCount, uSnareCount, uHatCount;   // running hit counters
uniform float uMotion;                    // time that runs faster when the music is louder
uniform sampler2D uWaveHistory;           // 256 x 64, row 0 newest
uniform sampler2D uSpectrumHistory;       // 128 x 64, row 0 newest
uniform float uHistoryPhase;              // 0..1 progress toward the next history row

in vec2 vUv;
out vec4 fragColor;

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

float hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float vnoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p)
{
    float amplitude = 0.5, sum = 0.0;
    for (int i = 0; i < 5; ++i)
    {
        sum += amplitude * vnoise(p);
        p = p * 2.03 + 17.1;
        amplitude *= 0.5;
    }
    return sum;
}

vec3 oklabToLinear(vec3 c)
{
    float l_ = c.x + 0.3963377774 * c.y + 0.2158037573 * c.z;
    float m_ = c.x - 0.1055613458 * c.y - 0.0638541728 * c.z;
    float s_ = c.x - 0.0894841775 * c.y - 1.2914855480 * c.z;
    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return vec3( 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

// The palette as a gradient, mixed in OKLab. t = 0 is the darkest colour, 1 the brightest.
// With Color Shift above zero the gradient becomes a loop and rotates.
vec3 palette(float t)
{
    bool looped = uColorShift > 0.0005;
    float x = looped ? fract(t * 0.8 + uColorShift) * 5.0 : clamp(t, 0.0, 1.0) * 4.0;
    float i = floor(x);
    int a = int(mod(i, 5.0));
    int b = looped ? int(mod(i + 1.0, 5.0)) : min(a + 1, 4);
    return max(oklabToLinear(mix(uPaletteLab[a], uPaletteLab[b], x - i)), vec3(0.0));
}

float spectrumAt(float x)   { return texture(uSpectrum, vec2(x, 0.5)).r; }

float luma(vec3 c)          { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Modules write premultiplied linear HDR colour; alpha is coverage for the compositor.
vec4 emit(vec3 colour)      { return vec4(colour, clamp(max(colour.r, max(colour.g, colour.b)), 0.0, 1.0)); }
