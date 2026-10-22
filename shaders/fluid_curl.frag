#version 450

// Scalar curl (vorticity) of the velocity field.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Grid {
    vec4 texel;
} g;

layout(set = 1, binding = 0) uniform sampler2D velocityField; // nearest

layout(location = 0) out vec4 outValue;

void main() {
    vec2 t = g.texel.xy;
    float left = texture(velocityField, vUV - vec2(t.x, 0.0)).y;
    float right = texture(velocityField, vUV + vec2(t.x, 0.0)).y;
    float up = texture(velocityField, vUV - vec2(0.0, t.y)).x;
    float down = texture(velocityField, vUV + vec2(0.0, t.y)).x;
    outValue = vec4(0.5 * ((right - left) - (down - up)), 0.0, 0.0, 1.0);
}
