// Editor overlay: an outline and corner handles around the selected widget. Never used in renders
// or exports.

uniform vec4  uRect;        // selected widget in viewport uv: x0, y0, x1, y1 (origin bottom-left)
uniform vec2  uViewport;    // viewport size in pixels
uniform vec3  uAccent;      // linear RGB

void main()
{
    vec2 px = vUv * uViewport;
    vec2 lo = uRect.xy * uViewport, hi = uRect.zw * uViewport;

    // Distance to the rectangle's edge, in pixels.
    vec2  d    = max(lo - px, px - hi);
    float edge = abs(max(d.x, d.y));
    float inside = step(max(d.x, d.y), 1.5);
    float line = (1.0 - smoothstep(0.75, 1.5, edge)) * inside;

    // 9 px square handles on the corners.
    float handle = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        vec2 corner = vec2(i == 0 || i == 2 ? lo.x : hi.x, i < 2 ? lo.y : hi.y);
        vec2 q = abs(px - corner);
        handle = max(handle, 1.0 - smoothstep(4.0, 5.0, max(q.x, q.y)));
    }

    float a = max(line * 0.9, handle);
    fragColor = vec4(uAccent * a, a);
}
