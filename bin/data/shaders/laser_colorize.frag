#pragma include "palette.glslinc"

uniform sampler2D srcTex;  // the low-res note-roll buffer (intensity in R)
uniform int paletteId;

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // The roll buffer stores beam INTENSITY; color comes from pitch --
    // each note's screen column gets its own palette slot, Synthesia
    // style. Nearest-filtered upscale of the tiny buffer supplies the
    // chunky 90s raster look for free.
    float intensity = texture(srcTex, texCoordVarying).r;
    vec3 color = palettePreset(texCoordVarying.x * 0.85, paletteId);
    // Normalize to full brightness, keeping only the hue: every pitch
    // must read equally -- a beam's visibility can't depend on whether
    // its palette slot happens to be a dark one.
    color /= max(max(color.r, max(color.g, color.b)), 0.3);
    fragColor = vec4(color * intensity, 1.0);
}
