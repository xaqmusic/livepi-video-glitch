uniform sampler2D srcTex;
uniform float amount;  // 0..1 curvature

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // The physical monitor, not a signal effect: the picture bulges like
    // it's on curved glass, with a soft vignette at the corners. Belongs
    // LAST in the post chain -- everything else happens "on the tube",
    // this is the tube (docs/videosynth-effects.md).
    vec2 n = texCoordVarying * 2.0 - 1.0;
    float r2 = dot(n, n);
    vec2 warped = n * (1.0 - amount * 0.22 * r2);
    vec2 uv = warped * 0.5 + 0.5;

    vec4 color = texture(srcTex, clamp(uv, 0.0, 1.0));

    // Black outside the bulged frame + gentle corner vignette.
    float inside = step(abs(warped.x), 1.0) * step(abs(warped.y), 1.0);
    float vignette = 1.0 - amount * 0.35 * r2;
    fragColor = vec4(color.rgb * inside * vignette, 1.0);
}
