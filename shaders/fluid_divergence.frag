#version 450

// Divergence of the velocity field; solid cells count as zero velocity.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Grid {
    vec4 texel; // xy: 1 / grid size
} g;

layout(set = 1, binding = 0) uniform sampler2D velocityField; // nearest
layout(set = 1, binding = 1) uniform sampler2D obstacles;     // nearest

layout(location = 0) out vec4 outValue;

vec2 velocityAt(vec2 uv) {
    return texture(obstacles, uv).r > 0.5 ? vec2(0.0) : texture(velocityField, uv).xy;
}

void main() {
    vec2 t = g.texel.xy;
    float left = velocityAt(vUV - vec2(t.x, 0.0)).x;
    float right = velocityAt(vUV + vec2(t.x, 0.0)).x;
    float up = velocityAt(vUV - vec2(0.0, t.y)).y;
    float down = velocityAt(vUV + vec2(0.0, t.y)).y;
    outValue = vec4(0.5 * ((right - left) + (down - up)), 0.0, 0.0, 1.0);
}
