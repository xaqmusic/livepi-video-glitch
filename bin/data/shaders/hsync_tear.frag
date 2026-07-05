#pragma include "common.glslinc"

uniform sampler2D srcTex;
uniform float noisePhase;
uniform float beatSpike;
uniform float intensity;

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // Shift each scanline sideways by a noise value that's tall (many
    // distinct bands vertically) but slowly varying in time, then punch a
    // hard, decaying spike right after a beat -- see docs HLD: "the video
    // frame snaps sideways and rips apart" on beat 2.
    float lineNoise = noise(texCoordVarying.y * 80.0 + noisePhase * 4.0) - 0.5;
    float displacement = lineNoise * intensity * (0.02 + beatSpike * 0.15);

    vec2 uv = vec2(texCoordVarying.x + displacement, texCoordVarying.y);
    fragColor = texture(srcTex, uv);
}
