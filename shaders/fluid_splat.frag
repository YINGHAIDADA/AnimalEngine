#version 450

// Adds up to 8 Gaussian splats (forces or dye) to a field.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Splats {
    vec4 info;     // x: splat count, y: grid aspect (width / height)
    vec4 shape[8]; // xy: center uv, z: radius in uv-height units
    vec4 value[8]; // amount added at the center
} s;

layout(set = 1, binding = 0) uniform sampler2D targetField; // nearest
layout(set = 1, binding = 1) uniform sampler2D obstacles;   // nearest

layout(location = 0) out vec4 outValue;

void main() {
    if (texture(obstacles, vUV).r > 0.5) {
        outValue = vec4(0.0);
        return;
    }
    vec4 v = texture(targetField, vUV);
    int count = int(s.info.x);
    for (int i = 0; i < 8; ++i) {
        if (i >= count) {
            break;
        }
        vec2 d = vUV - s.shape[i].xy;
        d.x *= s.info.y;
        float r = s.shape[i].z;
        v += s.value[i] * exp(-dot(d, d) / (r * r));
    }
    outValue = v;
}
