// Capsule line with an analytic anti-aliased core and a soft glow. Drawn with MAX blending so
// neighbouring segments join without bright beads.

uniform float uThickness, uGlow;

in vec2  vLocal;
in float vLen;
in float vTone;
in float vFade;

void main()
{
    float d    = length(vec2(vLocal.x - clamp(vLocal.x, 0.0, vLen), vLocal.y));
    float core = clamp(uThickness + 0.5 - d, 0.0, 1.0);
    float glow = exp(-d * d / (uGlow * uGlow * 0.18)) * 0.35;

    fragColor = emit(palette(vTone) * (core * 3.5 + glow) * vFade * uIntensity);
}
