#version 450

// Bloom source: bright parts of the canvas, downsampled 2× (linear sampling averages 2×2).

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Bright {
    vec4 params; // x: threshold, y: soft knee width
} u;

layout(set = 1, binding = 0) uniform sampler2D canvas; // linear

layout(location = 0) out vec4 outValue;

void main() {
    vec3 color = texture(canvas, vUV).rgb;
    float level = max(color.r, max(color.g, color.b));
    float weight = smoothstep(u.params.x, u.params.x + u.params.y, level);
    outValue = vec4(color * weight, 1.0);
}
