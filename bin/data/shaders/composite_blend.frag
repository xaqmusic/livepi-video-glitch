// Blends one layer onto the composite accumulated so far -- the single
// shader LayerCompositor runs once per layer, ping-ponging between two
// FBOs (GLES2 can't sample the FBO being written). Blend math lives here
// in the fragment shader rather than glBlendFunc state so the result is
// deterministic across the desktop GL and Pi GLES2 drivers.
//
// blendMode values match the BlendMode enum in src/scenes/Scene.h:
// 0 normal, 1 add, 2 screen, 3 multiply. Keep in sync.

uniform sampler2D accumTex;  // everything below this layer, already composited
uniform sampler2D layerTex;  // this layer's own effect-chain output
uniform int blendMode;
uniform float opacity;

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    vec3 base = texture(accumTex, texCoordVarying).rgb;
    vec4 layer = texture(layerTex, texCoordVarying);

    // Layer alpha (straight, from e.g. the note-laser colorize pass) makes
    // transparent regions leave the composite untouched in EVERY mode --
    // for the opaque layers (clips, full-frame generators, a=1) each
    // branch reduces to the plain formula.
    vec3 blended;
    if (blendMode == 1) {
        blended = base + layer.rgb * layer.a;
    } else if (blendMode == 2) {
        blended = 1.0 - (1.0 - base) * (1.0 - layer.rgb * layer.a);
    } else if (blendMode == 3) {
        blended = base * mix(vec3(1.0), layer.rgb, layer.a);
    } else {
        blended = mix(base, layer.rgb, layer.a);
    }

    fragColor = vec4(mix(base, clamp(blended, 0.0, 1.0), opacity), 1.0);
}
