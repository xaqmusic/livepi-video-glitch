uniform sampler2D srcTex;   // previous heat field (R channel)
uniform vec2 texel;         // 1/bufferSize
uniform float seedPhase;    // advances per SIM STEP -- drives the flicker
uniform float height;       // 0..1, inverse cooling = flame height
uniform float down;         // 0 = burns bottom-up, 1 = burns top-down

in vec2 texCoordVarying;
out vec4 fragColor;

// One step of the classic demoscene fire: each cell pulls heat from the
// cells "below" it (toward the seed edge) and cools a jittered amount.
// 8-bit heat + per-cell cooling noise = licking flame tongues for free.

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    // Toward the seed edge: below for bottom-up, above for top-down.
    float dir = mix(1.0, -1.0, down);
    vec2 uv = texCoordVarying;

    // Seed rows at the origin edge: chunky 4-texel-wide embers whose
    // brightness re-rolls every sim step.
    float edgeDist = mix(1.0 - uv.y, uv.y, down);  // 0 at the seed edge
    if (edgeDist < texel.y * 2.0) {
        float cell = floor(uv.x / (texel.x * 4.0));
        float ember = hash21(vec2(cell, floor(seedPhase)));
        // Most cells run hot, some drop out entirely -- gaps make the
        // flame base ragged instead of a solid bar.
        float heat = ember < 0.15 ? 0.0 : 0.75 + 0.25 * hash21(vec2(cell + 57.0, floor(seedPhase)));
        fragColor = vec4(vec3(heat), 1.0);
        return;
    }

    float below1 = texture(srcTex, uv + vec2(-texel.x, dir * texel.y)).r;
    float below2 = texture(srcTex, uv + vec2(0.0, dir * texel.y)).r;
    float below3 = texture(srcTex, uv + vec2(texel.x, dir * texel.y)).r;
    float below4 = texture(srcTex, uv + vec2(0.0, dir * texel.y * 2.0)).r;
    float heat = (below1 + below2 + below3 + below4) * 0.25;

    // Jittered cooling: height 1 barely cools (flames reach the far
    // edge), height 0 snuffs everything within a few rows.
    float jitter = hash21(uv * 251.0 + vec2(seedPhase * 0.37, seedPhase));
    float cooling = (0.002 + (1.0 - height) * 0.05) * (0.5 + jitter);
    heat = max(heat - cooling, 0.0);

    fragColor = vec4(vec3(heat), 1.0);
}
