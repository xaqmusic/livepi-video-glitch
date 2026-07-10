uniform sampler2D srcTex;
uniform float amount;      // 0 clean .. 1 dead-channel snow (master)
uniform float scale;       // 0 fine grain .. 1 chunky blocks
uniform float brightness;  // white level of the grain
uniform float blur;        // 0 crisp specks .. 1 cloudy wash
uniform float frameSeed;   // re-rolls the grain each frame (wrapped 0..256)
uniform vec2 resolution;

in vec2 texCoordVarying;
out vec4 fragColor;

// Noise hash sized for the Pi (GLSL ES 1.00, mediump float). No tile wrap:
// with the grain capped at ~140 cells the coordinate stays inside mediump's
// guaranteed range, so the sin() argument never overflows AND the pattern
// never repeats on screen (an earlier mod-wrap recurred a few times across
// the width and read as faint vertical banding). Balanced dot constants keep
// BOTH axes' cell-to-cell step above the mediump precision floor, so neither
// direction smears; the per-frame seed folds into the phase to re-roll.
float hash(vec2 cell) {
    return fract(sin(dot(cell, vec2(31.0, 43.0)) + frameSeed) * 43758.5453);
}

void main() {
    vec4 src = texture(srcTex, texCoordVarying);

    // Square grain cells regardless of the 16:9 frame; chunky (few) at
    // scale 1, fine (many) at scale 0. Capped at 140 so cell coords stay in
    // mediump range on the Pi.
    float cells = mix(140.0, 18.0, scale);
    vec2 g = texCoordVarying * vec2(cells * (resolution.x / resolution.y), cells);
    vec2 i = floor(g);
    vec2 f = fract(g);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    // blur = 0 keeps the crisp nearest-cell speck; blur = 1 smoothly
    // interpolates the cell corners into a soft cloudy wash.
    vec2 u = f * f * (3.0 - 2.0 * f);
    float soft = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
    float n = mix(a, soft, blur) * brightness;

    // The grain overtakes the signal as amount rises: a light haze low, a
    // full snow field at 1.
    fragColor = vec4(mix(src.rgb, vec3(n), amount), src.a);
}
