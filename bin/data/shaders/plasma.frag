#pragma include "palette.glslinc"

uniform float phase;
uniform float plasmaScale;  // how busy the pattern reads
uniform int paletteId;

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // The quintessential demo effect: a sum of sines through the shared
    // cosine palette (docs/videosynth-effects.md). Never samples srcTex --
    // this pass PAINTS, the rest of the layer chain then warps/quantizes
    // it like any footage.
    vec2 uv = texCoordVarying * plasmaScale;
    float p = sin(uv.x * 3.1 + phase)
            + sin(uv.y * 2.3 + phase * 1.3)
            + sin((uv.x + uv.y) * 1.7 + phase * 0.7)
            + sin(length(texCoordVarying - 0.5) * plasmaScale * 4.0 - phase * 1.7);

    fragColor = vec4(palettePreset(p * 0.125, paletteId), 1.0);
}
