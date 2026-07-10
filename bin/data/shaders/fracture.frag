uniform sampler2D srcTex;
uniform float amount;      // 0 intact .. 1 blown apart (THE performance knob)
uniform float cells;       // shard density across the frame
uniform float phase;       // drift, CPU-accumulated (held breaks stay alive)
uniform float jitterAmt;   // stepped trembling strength (CONTENT in shards)
uniform float jitterStep;  // advances ~14x/sec CPU-side; each step = new twitch
uniform float scatterStep; // quantized scatter value; each step = re-rolled
                           // flight directions (the chaos re-arrange handle)
uniform float quakeAmt;    // stepped trembling of the CRACK PATTERN itself
uniform float rebreakStep; // quantized re-crack: each step = entirely new
                           // fracture lines (vs scatter, which keeps them)
uniform float spread;      // flight DISTANCE of detached pieces: 0 = broken
                           // but held in place, 1 = launched off screen.
                           // Decoupled from `amount` so the break can be
                           // established first and detonated separately.

in vec2 texCoordVarying;
out vec4 fragColor;

// Voronoi shatter (docs request: RANDOM fracture, not a regular mosaic).
// A jittered Voronoi partition carves the frame into irregular shards;
// each shard is RIGID -- one random displacement + slight tilt from its
// cell hash, scaled by `amount`, so pieces fly apart coherently and
// reassemble perfectly at 0. Cracks between shards go TRANSPARENT
// (alpha 0): whatever plays beneath the layer shows through the gaps.

vec2 hash22(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                          dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

void main() {
    vec2 uv = texCoordVarying;
    // Aspect-corrected grid so shards read roughly isotropic on 16:9.
    vec2 g = uv * vec2(cells * 1.78, cells);

    // 3x3 Worley search: nearest (F1) and second-nearest (F2) jittered
    // points. F2-F1 measures distance to the shard boundary -> cracks.
    vec2 shardId = vec2(0.0);
    float f1 = 1e9;
    float f2 = 1e9;
    // Rebreak re-keys the whole partition: a different rebreakStep hashes
    // every cell differently -> entirely new crack lines. Quake adds a
    // stepped twitch to the seed points -> the pattern itself shudders.
    // Smooth drift (the phase orbit) LAYERS ON TOP of both.
    vec2 rebreakKey = vec2(rebreakStep * 7.3, rebreakStep * 11.9);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 cell = floor(g) + vec2(float(dx), float(dy));
            vec2 seed = hash22(cell + rebreakKey);
            vec2 pt = cell + 0.5 + 0.35 * vec2(cos(6.28318 * seed.x + phase),
                                               sin(6.28318 * seed.y + phase));
            pt += (hash22(cell + rebreakKey + vec2(jitterStep * 23.3, jitterStep * 9.1)) - 0.5)
                  * quakeAmt * 0.6;
            float d = distance(g, pt);
            if (d < f1) {
                f2 = f1;
                f1 = d;
                shardId = cell;
            } else if (d < f2) {
                f2 = d;
            }
        }
    }

    // Scatter re-keys the per-shard randomness: a different scatterStep is
    // a completely different arrangement of flight directions/tilts (the
    // shard SHAPES stay put -- it reads as the same break re-scattering,
    // not a new fracture). Content seeds also fold in the rebreak key, so
    // a new break scatters anew.
    vec2 seed = hash22(shardId + rebreakKey + vec2(scatterStep * 13.7, scatterStep * 5.3));
    vec2 dir = seed - 0.5;

    // Jitter: stepped twitch, not a glide -- each jitterStep is a fresh
    // random offset, so pieces tremble like a broken signal.
    vec2 twitch = hash22(shardId + vec2(jitterStep * 31.1, jitterStep * 17.3)) - 0.5;
    dir += twitch * jitterAmt * 0.9;

    // STAGED breaking: every shard has its own detach threshold, so the
    // image cracks progressively -- hairline stress fractures first, then
    // pieces let go one by one, the loose ones flying farther. Uniform
    // simultaneous separation read as stained glass, not as breaking.
    float thresh = seed.y * 0.35;
    float letsGo = clamp((amount - thresh) / max(1.0 - thresh, 0.001), 0.0, 1.0);
    float mag = letsGo * letsGo * spread * 1.2 * (0.4 + 0.8 * seed.x);

    // Rigid-shard sampling: displace, plus a slight tilt around the shard
    // center (pieces rotate as they separate -- sells "broken").
    vec2 center = (shardId + 0.5) / vec2(cells * 1.78, cells);
    float ang = ((seed.x - 0.5) * 1.2 + twitch.y * jitterAmt * 0.7) * letsGo;
    float s = sin(ang);
    float c = cos(ang);
    vec2 rel = uv - center;
    rel = vec2(rel.x * c - rel.y * s, rel.x * s + rel.y * c);
    vec2 srcUv = center + rel - dir * mag;

    // Gaps open per shard as it lets go; still-attached shards show only
    // hairline cracks (the narrow smoothstep band at gap 0). The top
    // stretch of the master slider (0.75..1) adds a DISPERSAL tail: gaps
    // keep growing until every piece erodes away and the frame is pure
    // void -- "push all the pieces off the viewable area". Below 0.75
    // this term is zero and the established break arc is unchanged.
    float disperse = smoothstep(0.75, 1.0, amount);
    float gap = letsGo * 0.09 + disperse * disperse * 1.15;
    float crack = smoothstep(gap, gap + 0.015 + 0.05 * amount, f2 - f1);

    if (srcUv.x < 0.0 || srcUv.x > 1.0 || srcUv.y < 0.0 || srcUv.y > 1.0) {
        // A shard displaced past the frame edge shows void, not smear.
        fragColor = vec4(0.0);
        return;
    }

    // Per-shard chromatic split along the displacement direction: the
    // broken-hologram read. Scales with amount, zero when intact.
    vec2 split = dir * mag * 0.15;
    float r = texture(srcTex, clamp(srcUv + split, 0.0, 1.0)).r;
    vec4 base = texture(srcTex, srcUv);
    float b = texture(srcTex, clamp(srcUv - split, 0.0, 1.0)).b;

    // Shards catch the light differently once broken.
    float shade = mix(1.0, 0.7 + 0.6 * seed.y, letsGo);

    fragColor = vec4(vec3(r, base.g, b) * shade, base.a * crack);
}
