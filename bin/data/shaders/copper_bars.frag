#pragma include "palette.glslinc"

uniform float phase;
uniform float barCount;
uniform float sharpness;  // 0 soft gradient bands .. 1 hard copper stripes
uniform int paletteId;

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // Named for the Amiga's Copper racing the beam: horizontal color bands
    // sweeping the screen (docs/videosynth-effects.md). Blend mode +
    // opacity on the layer decide whether it tints or screens over the
    // footage below.
    float wave = sin(texCoordVarying.y * barCount * 6.28318 + phase);
    float band = 0.5 + 0.5 * wave;
    // Sharpen toward hard-edged stripes as sharpness rises.
    band = pow(band, 1.0 + sharpness * 6.0);

    vec3 color = palettePreset(texCoordVarying.y + phase * 0.05, paletteId) * band;
    fragColor = vec4(color, 1.0);
}
