#pragma include "palette.glslinc"

uniform sampler2D srcTex;  // heat field (R channel)
uniform int paletteId;

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // Heat walks the cosine palette BACKWARD from its dark trough into
    // its bright wrap: for lava that's near-black navy -> deep red ->
    // orange -> yellow-white, i.e. the classic fire ramp (the forward
    // direction runs through lava's teal midsection and reads nothing
    // like fire -- found on the first burn). Phosphor gives ghost
    // flames, vaporwave a synth inferno. Straight alpha from heat: cold
    // cells are TRANSPARENT, so fire overlays whatever plays beneath.
    float heat = texture(srcTex, texCoordVarying).r;
    vec3 color = palettePreset(0.55 - heat * 0.65, paletteId);
    float alpha = smoothstep(0.04, 0.35, heat);
    fragColor = vec4(color, alpha);
}
