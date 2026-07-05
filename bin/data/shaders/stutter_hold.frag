// The stutter/freeze effect (docs HLD: "freezes a single frame or loops 3
// frames rapidly") is implemented CPU-side by StutterBufferPass, which
// selects *which* buffered frame's texture to feed in as srcTex each draw.
// This shader is intentionally a pure passthrough.

uniform sampler2D srcTex;

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    fragColor = texture(srcTex, texCoordVarying);
}
