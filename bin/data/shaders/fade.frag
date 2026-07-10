uniform sampler2D srcTex;
uniform float fade;  // 0 = untouched, 1 = black

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // Transition dip-to-black; last in the post chain so it darkens the
    // finished frame (tube curve and all).
    vec4 src = texture(srcTex, texCoordVarying);
    fragColor = vec4(src.rgb * (1.0 - fade), src.a);
}
