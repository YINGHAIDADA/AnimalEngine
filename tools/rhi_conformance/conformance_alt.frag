#version 450

// conformance.frag with inverted colours and the same resources: the shader hot reload check swaps
// it in and out of the live pipelines.

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Draw {
    vec4 tint;
    vec4 target;
} draw;

layout(set = 1, binding = 0) uniform sampler2D image;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 base = draw.target.z > 0.5 ? texture(image, vUV) : vec4(vColor, 1.0);
    outColor = vec4(1.0 - base.rgb, base.a) * draw.tint;
}
