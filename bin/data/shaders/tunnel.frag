uniform sampler2D srcTex;
uniform float tunnelPhase;  // accumulated CPU-side from tunnel.speed
uniform float amount;       // 0..1 mix between flat video and the tunnel

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // Polar remap using the VIDEO ITSELF as the tunnel wall -- the footage
    // gets pulled through a drain rather than the demoscene's brick
    // texture (docs/videosynth-effects.md).
    vec2 centered = texCoordVarying - 0.5;
    float r = max(length(centered), 0.02);
    float a = atan(centered.y, centered.x);

    vec2 tunnelUv = vec2(fract(0.15 / r + tunnelPhase), a / 6.28318 + 0.5);
    vec4 tunnelColor = texture(srcTex, tunnelUv);

    // Darken toward the center so the "depth" reads.
    tunnelColor.rgb *= clamp(r * 2.5, 0.15, 1.0);

    vec4 flat_ = texture(srcTex, texCoordVarying);
    fragColor = mix(flat_, tunnelColor, amount);
}
