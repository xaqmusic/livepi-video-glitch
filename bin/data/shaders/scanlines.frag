uniform sampler2D srcTex;
uniform float intensity;  // 0..1 how dark the gaps between lines get
uniform float zoom;       // 0 far (fine, dense) .. 1 close (few, thick)

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    vec4 src = texture(srcTex, texCoordVarying);

    // Line pitch = how close the CRT feels: up close you count a few thick
    // lines; far away they pack fine and dense until the picture reads solid.
    float lines = mix(500.0, 32.0, zoom);
    float wave = 0.5 + 0.5 * sin(texCoordVarying.y * lines * 6.28318);

    // Bright rows stay full, troughs darken by intensity.
    float scan = 1.0 - intensity * (1.0 - wave);
    fragColor = vec4(src.rgb * scan, src.a);
}
