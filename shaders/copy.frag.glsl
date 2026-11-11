// Draws a texture over the bound target (widget compositing, trail fades).

uniform sampler2D uSource;
uniform float uOpacity;

void main()
{
    fragColor = texture(uSource, vUv) * uOpacity;
}
