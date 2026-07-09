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
    //
    // The intensity response is deliberately cubic: the low end stays a
    // subtle wobble, but max is total obliteration -- displacement past a
    // full screen width, band count climbing so the frame shreds into
    // thin strips, and fract() wrap pulling torn content from anywhere in
    // the picture instead of smearing the edges.
    float bands = 80.0 + intensity * 240.0;
    float lineNoise = noise(texCoordVarying.y * bands + noisePhase * 4.0) - 0.5;
    float depth = 0.04 * intensity + 2.2 * intensity * intensity * intensity;
    float displacement = lineNoise * (depth + beatSpike * (0.15 + intensity * 0.5));

    vec2 uv = vec2(fract(texCoordVarying.x + displacement), texCoordVarying.y);
    fragColor = texture(srcTex, uv);
}
