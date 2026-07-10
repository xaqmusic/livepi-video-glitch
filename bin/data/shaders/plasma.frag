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

    vec3 color = palettePreset(p * 0.125, paletteId);
    // Straight alpha keyed off brightness: the dark troughs of the field go
    // transparent so plasma reads as a glow over the layers beneath, opaque
    // where it's bright. Alone over black it looks the same as a solid field.
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    fragColor = vec4(color, smoothstep(0.03, 0.55, lum));
}
