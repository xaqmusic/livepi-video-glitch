uniform sampler2D srcTex;
uniform float separation;  // 0..1, resolved live (knobs/audio/scene baseline)

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // Cubic response: gentle color fringing at the low end, but max rips
    // the red and blue planes more than half a screen apart -- at that
    // point the picture stops reading as an image at all, which is the
    // intended ceiling ("absolutely extreme at maximum").
    float depth = 0.02 * separation + 0.55 * separation * separation * separation;
    vec2 offset = vec2(depth, 0.0);
    float r = texture(srcTex, texCoordVarying - offset).r;
    float g = texture(srcTex, texCoordVarying).g;
    float b = texture(srcTex, texCoordVarying + offset).b;
    fragColor = vec4(r, g, b, 1.0);
}
