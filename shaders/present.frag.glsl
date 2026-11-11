// Final pass: bloom, exposure, tone map, flash limiter, sRGB encode, blue-noise dither.
// With uMeasure = 1 it instead writes (luminance, red flash amount) of the displayed picture,
// which the flash limiter reads back.

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uBlueNoise;     // 64 x 64, ranks 0..1
uniform float uBloomStrength, uExposure;
uniform int   uTonemap;           // 0 Film (AgX), 1 Punchy (ACES fitted), 2 Neutral (Khronos PBR Neutral)
uniform int   uMeasure;
uniform float uGain, uRedDesaturation;
uniform ivec2 uNoiseOffset;
uniform sampler2D uFadeFrom;      // the previous scene's last frame during a crossfade
uniform float uFadeOld;           // 1 = all old scene, 0 = all new

// --- AgX (Troy Sobotka), minimal fitted form -----------------------------------------------------
vec3 agxContrast(vec3 x)
{
    vec3 x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 c)
{
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393, maxEv = 4.026069;

    c = clamp(log2(max(inset * c, vec3(1.0e-10))), minEv, maxEv);
    c = agxContrast((c - minEv) / (maxEv - minEv));
    return pow(max(outset * c, vec3(0.0)), vec3(2.2));
}

// --- ACES fitted (Stephen Hill) -----------------------------------------------------------------
vec3 acesFitted(vec3 c)
{
    const mat3 inputMat  = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 outputMat = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    c = inputMat * c;
    c = (c * (c + 0.0245786) - 0.000090537) / (c * (0.983729 * c + 0.4329510) + 0.238081);
    return clamp(outputMat * c, 0.0, 1.0);
}

// --- Khronos PBR Neutral ------------------------------------------------------------------------
vec3 neutral(vec3 c)
{
    const float start = 0.76, desaturation = 0.15;
    float x = min(c.r, min(c.g, c.b));
    c -= x < 0.08 ? x - 6.25 * x * x : 0.04;
    float peak = max(c.r, max(c.g, c.b));
    if (peak < start)
        return c;
    const float d = 1.0 - start;
    float newPeak = 1.0 - d * d / (peak + d - start);
    c *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(c, vec3(newPeak), g);
}

vec3 linearToSrgb(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

float redAmount(vec3 c)
{
    float share = c.r / (c.r + c.g + c.b + 1.0e-4);
    return smoothstep(0.7, 0.8, share) * max(c.r - c.g - c.b, 0.0);
}

void main()
{
    vec3 hdr = texture(uScene, vUv).rgb + texture(uBloom, vUv).rgb * (uBloomStrength / 6.0);
    if (uFadeOld > 0.0)
        hdr = mix(hdr, texture(uFadeFrom, vUv).rgb, uFadeOld);
    hdr *= uExposure;

    vec3 c = uTonemap == 1 ? acesFitted(hdr) : (uTonemap == 2 ? neutral(hdr) : agx(hdr));
    c = clamp(c, 0.0, 1.0);

    if (uMeasure == 1)
    {
        fragColor = vec4(luma(c), redAmount(c), 0.0, 1.0);
        return;
    }

    c *= uGain;
    float redShare = smoothstep(0.7, 0.8, c.r / (c.r + c.g + c.b + 1.0e-4));
    c = mix(c, vec3(luma(c)), uRedDesaturation * redShare);

    // Blue noise, spread over +-1 code value, removes banding before the 8-bit output.
    float n = texelFetch(uBlueNoise, (ivec2(gl_FragCoord.xy) + uNoiseOffset) & 63, 0).r;
    fragColor = vec4(linearToSrgb(c) + (n - 0.5) / 255.0, 1.0);
}
