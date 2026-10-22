#version 450

// Vorticity confinement: pushes the flow along the gradient of |curl| to restore the small
// swirls that the coarse grid and semi-Lagrangian advection smear out.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Vorticity {
    vec4 texel;  // xy: 1 / grid size
    vec4 params; // x: dt, y: confinement strength
} u;

layout(set = 1, binding = 0) uniform sampler2D velocityField; // nearest
layout(set = 1, binding = 1) uniform sampler2D curlField;     // nearest
layout(set = 1, binding = 2) uniform sampler2D obstacles;     // nearest

layout(location = 0) out vec4 outValue;

void main() {
    vec2 velocity = texture(velocityField, vUV).xy;
    if (texture(obstacles, vUV).r > 0.5) {
        outValue = vec4(0.0);
        return;
    }
    vec2 t = u.texel.xy;
    float left = abs(texture(curlField, vUV - vec2(t.x, 0.0)).r);
    float right = abs(texture(curlField, vUV + vec2(t.x, 0.0)).r);
    float up = abs(texture(curlField, vUV - vec2(0.0, t.y)).r);
    float down = abs(texture(curlField, vUV + vec2(0.0, t.y)).r);
    float curl = texture(curlField, vUV).r;

    vec2 gradient = 0.5 * vec2(right - left, down - up);
    float magnitude = length(gradient);
    vec2 n = magnitude > 1e-5 ? gradient / magnitude : vec2(0.0);
    vec2 force = u.params.y * curl * vec2(n.y, -n.x);
    outValue = vec4(velocity + force * u.params.x, 0.0, 1.0);
}
