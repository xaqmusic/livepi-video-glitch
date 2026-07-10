uniform sampler2D srcTex;    // current layer content (straight alpha)
uniform sampler2D trailTex;  // previous trail buffer (straight alpha)
uniform float decay;         // per-frame coverage kept by the echo (0..1)

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    vec4 src = texture(srcTex, texCoordVarying);
    vec4 prev = texture(trailTex, texCoordVarying);

    // Premultiplied "lighten" feedback: the echo survives where it's brighter
    // or more present than the current frame, and loses `decay` of its
    // coverage each frame. The ALPHA carries a transparent layer's echoes all
    // the way to FULL transparency (no opaque ghost left behind); the RGB
    // carries glowing trails on an opaque layer. Working premultiplied keeps
    // the straight-alpha compositing correct.
    vec3 srcPm = src.rgb * src.a;
    vec3 echoPm = prev.rgb * prev.a * decay;
    float outA = max(src.a, prev.a * decay);
    vec3 outPm = max(srcPm, echoPm);

    vec3 rgb = outA > 0.0001 ? outPm / outA : vec3(0.0);
    fragColor = vec4(rgb, outA);
}
