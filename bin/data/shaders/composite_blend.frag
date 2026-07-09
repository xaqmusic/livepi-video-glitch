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
    vec3 layer = texture(layerTex, texCoordVarying).rgb;

    vec3 blended;
    if (blendMode == 1) {
        blended = base + layer;
    } else if (blendMode == 2) {
        blended = 1.0 - (1.0 - base) * (1.0 - layer);
    } else if (blendMode == 3) {
        blended = base * layer;
    } else {
        blended = layer;
    }

    fragColor = vec4(mix(base, clamp(blended, 0.0, 1.0), opacity), 1.0);
}
