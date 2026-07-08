uniform sampler2D srcTex;
uniform float separation;  // 0..1, driven by audioLevel/scene intensity

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    vec2 offset = vec2(separation * 0.02, 0.0);
    float r = texture(srcTex, texCoordVarying - offset).r;
    float g = texture(srcTex, texCoordVarying).g;
    float b = texture(srcTex, texCoordVarying + offset).b;
    fragColor = vec4(r, g, b, 1.0);
}
