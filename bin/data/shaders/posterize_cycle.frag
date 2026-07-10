#pragma include "palette.glslinc"

uniform sampler2D srcTex;
uniform float levels;      // quantization bins, 2..8 (quantized CPU-side)
uniform float cyclePhase;  // accumulated CPU-side from posterize.speed
uniform int paletteId;     // 0 lava, 1 vaporwave, 2 phosphor
uniform float amount;      // 0..1 mix with the original

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // The defining Amiga trick, minus the indexed-color constraint: hard
    // luminance bins mapped through a drifting cosine palette -- "color
    // doing the work" (docs/videosynth-effects.md).
    vec4 src = texture(srcTex, texCoordVarying);
    float luma = dot(src.rgb, vec3(0.299, 0.587, 0.114));
    float bin = floor(luma * levels) / levels;

    vec3 cycled = palettePreset(bin + cyclePhase, paletteId);
    fragColor = vec4(mix(src.rgb, cycled, amount), src.a);
}
