#version 450

// Indirect light (ADR-0039), step one: the light buffer averaged over 4×4 canvas pixels (exact
// texel reads, the same on every backend), before the blur spreads it wide.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Downsample {
    vec4 params; // xy: source size in pixels
} u;

layout(set = 1, binding = 0) uniform sampler2D source; // the light buffer

layout(location = 0) out vec4 outValue;

void main() {
    ivec2 size = ivec2(u.params.xy);
    ivec2 base = ivec2(floor(vUV * u.params.xy * 0.25)) * 4;
    vec3 sum = vec3(0.0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            sum += texelFetch(source, min(base + ivec2(x, y), size - 1), 0).rgb;
        }
    }
    outValue = vec4(sum / 16.0, 1.0);
}
