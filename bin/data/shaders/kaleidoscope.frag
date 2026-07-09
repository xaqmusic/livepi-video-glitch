uniform sampler2D srcTex;
uniform float segments;  // wedge count, 3..12 (quantized CPU-side)

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // Fold the angle into N mirrored wedges, then reproject and sample --
    // one knob, completely different symmetry per step.
    vec2 centered = texCoordVarying - 0.5;
    float r = length(centered);
    float a = atan(centered.y, centered.x);

    float wedge = 6.28318 / segments;
    a = abs(mod(a, wedge) - wedge * 0.5);

    vec2 uv = vec2(cos(a), sin(a)) * r + 0.5;
    fragColor = texture(srcTex, clamp(uv, 0.0, 1.0));
}
