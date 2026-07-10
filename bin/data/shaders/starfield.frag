uniform float phase;
uniform float density;


in vec2 texCoordVarying;
out vec4 fragColor;

// Hashed-grid starfield (docs/videosynth-effects.md's recommended cheap
// version): stars from a per-cell hash, no persistent state, three
// parallax planes scrolling at different speeds.

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float starPlane(vec2 uv, float cells, float scroll) {
    vec2 scrolled = vec2(uv.x + scroll, uv.y);
    vec2 cell = floor(scrolled * cells);
    vec2 local = fract(scrolled * cells);

    float h = hash21(cell);
    if (h > 0.12) return 0.0;  // most cells are empty space

    vec2 starPos = vec2(hash21(cell + 7.0), hash21(cell + 13.0));
    float d = length(local - starPos);
    float brightness = 0.4 + 0.6 * hash21(cell + 29.0);
    return brightness * smoothstep(0.12, 0.0, d);
}

void main() {
    float cells = 8.0 + density * 32.0;
    float s = starPlane(texCoordVarying, cells, phase * 0.15)
            + starPlane(texCoordVarying, cells * 1.7, phase * 0.3) * 0.7
            + starPlane(texCoordVarying, cells * 2.9, phase * 0.6) * 0.45;

    // Straight alpha: stars are opaque, the empty space between them is
    // transparent, so the starfield overlays the layers beneath. Full-white
    // rgb with brightness carried by alpha (the note generators' convention).
    fragColor = vec4(vec3(1.0), min(s, 1.0));
}
