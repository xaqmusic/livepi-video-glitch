uniform sampler2D srcTex;  // the low-res blob field (colour accumulates in RGB)
uniform float pixelate;    // 0 = fine brightness steps, 1 = chunky steps

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    vec3 c = texture(srcTex, texCoordVarying).rgb;
    float lum = max(c.r, max(c.g, c.b));

    // Posterize the brightness so the decay drops in visible steps rather
    // than a smooth ramp -- the "pixelated decay". Fewer levels = chunkier.
    // The nearest-filtered upscale of the tiny field supplies the blocky
    // pixels for free; this supplies the stepped fade.
    float levels = mix(20.0, 4.0, pixelate);
    float q = floor(lum * levels + 0.5) / levels;

    // Keep the accumulated hue, apply the quantized brightness.
    vec3 rgb = (lum > 0.001 ? c / lum : vec3(0.0)) * q;

    // Straight (unpremultiplied) alpha keyed off the quantized level: the
    // layer is TRANSPARENT wherever the field is dark, so the blobs float
    // over whatever plays beneath instead of on a black card.
    float alpha = smoothstep(0.03, 0.18, q);
    fragColor = vec4(rgb, alpha);
}
