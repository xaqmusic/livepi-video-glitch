#pragma include "common.glslinc"

uniform sampler2D srcTex;
uniform float noisePhase;
uniform float beatSpike;
uniform float intensity;

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // The vertical-strip sibling of hsync_tear: offset each COLUMN by a
    // noise value instead of each scanline, same cubic obliteration curve
    // and beat-spike behavior.
    float bands = 60.0 + intensity * 180.0;
    float columnNoise = noise(texCoordVarying.x * bands + noisePhase * 3.0) - 0.5;
    float depth = 0.04 * intensity + 2.2 * intensity * intensity * intensity;
    float displacement = columnNoise * (depth + beatSpike * (0.15 + intensity * 0.5));

    vec2 uv = vec2(texCoordVarying.x, fract(texCoordVarying.y + displacement));
    fragColor = texture(srcTex, uv);
}
