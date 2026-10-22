#version 450

// RHI conformance (ADR-0025): positions in target pixels, a per-instance offset, colour and uv.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;   // Float3 attribute
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec2 inOffset;  // per instance

layout(set = 0, binding = 0, std140) uniform Draw {
    vec4 tint;
    vec4 target; // xy: target size in pixels, z: 1 = sample the texture, 0 = vertex colour
} draw;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    vec2 pixel = inPosition + inOffset;
    gl_Position = vec4(pixel / draw.target.xy * 2.0 - 1.0, 0.0, 1.0);
    vColor = inColor;
    vUV = inUV;
}
