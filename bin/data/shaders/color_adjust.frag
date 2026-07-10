uniform sampler2D srcTex;
uniform float gain;        // 0..2, 1 = neutral (brightness as gain, not offset)
uniform float contrast;    // 0..2 around mid-gray, 1 = neutral
uniform float saturation;  // 0 = grayscale, 1 = neutral, 2 = oversaturated

in vec2 texCoordVarying;
out vec4 fragColor;

void main() {
    // One pass serves BOTH scopes: per-layer (in each layer chain) and the
    // global grade (first in the post chain) -- same keys, different param
    // scope. Order: gain, then contrast around mid-gray, then saturation
    // via luma mix. Alpha passes through untouched (transparent generator
    // layers keep their holes).
    vec4 src = texture(srcTex, texCoordVarying);
    vec3 c = src.rgb * gain;
    c = (c - 0.5) * contrast + 0.5;
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, saturation);
    fragColor = vec4(clamp(c, 0.0, 1.0), src.a);
}
