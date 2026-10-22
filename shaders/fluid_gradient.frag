#version 450

// Projection: subtracts the pressure gradient so the velocity field becomes divergence-free.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Grid {
    vec4 texel;
} g;

layout(set = 1, binding = 0) uniform sampler2D pressureField; // nearest
layout(set = 1, binding = 1) uniform sampler2D velocityField; // nearest
layout(set = 1, binding = 2) uniform sampler2D obstacles;     // nearest

layout(location = 0) out vec4 outValue;

float pressureAt(vec2 uv, float centre) {
    return texture(obstacles, uv).r > 0.5 ? centre : texture(pressureField, uv).r;
}

void main() {
    if (texture(obstacles, vUV).r > 0.5) {
        outValue = vec4(0.0);
        return;
    }
    vec2 t = g.texel.xy;
    float centre = texture(pressureField, vUV).r;
    float left = pressureAt(vUV - vec2(t.x, 0.0), centre);
    float right = pressureAt(vUV + vec2(t.x, 0.0), centre);
    float up = pressureAt(vUV - vec2(0.0, t.y), centre);
    float down = pressureAt(vUV + vec2(0.0, t.y), centre);
    vec2 velocity = texture(velocityField, vUV).xy - 0.5 * vec2(right - left, down - up);
    outValue = vec4(velocity, 0.0, 1.0);
}
