uniform sampler2D srcTex;
uniform float angle;      // radians, accumulated CPU-side from rotozoom.speed
uniform float zoomScale;  // sample-space scale: >1 zooms out, <1 zooms in

in vec2 texCoordVarying;
out vec4 fragColor;

// Mirror-repeat so zooming out tiles reflected copies instead of clamped
// streaks (docs/videosynth-effects.md: "needs mirror-repeat at the
// texture edges or corners go black").
vec2 mirrored(vec2 uv) {
    vec2 m = abs(mod(uv, 2.0) - 1.0);
    return 1.0 - m;
}

void main() {
    vec2 centered = texCoordVarying - 0.5;
    float c = cos(angle);
    float s = sin(angle);
    vec2 rotated = vec2(centered.x * c - centered.y * s,
                        centered.x * s + centered.y * c);
    vec2 uv = rotated * zoomScale + 0.5;
    fragColor = texture(srcTex, mirrored(uv));
}
