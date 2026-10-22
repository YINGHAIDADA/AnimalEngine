#version 450

// One Jacobi iteration of the pressure Poisson equation; solid neighbours mirror the centre
// pressure (Neumann boundary).

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Grid {
    vec4 texel;
} g;

layout(set = 1, binding = 0) uniform sampler2D pressureField;   // nearest
layout(set = 1, binding = 1) uniform sampler2D divergenceField; // nearest
layout(set = 1, binding = 2) uniform sampler2D obstacles;       // nearest

layout(location = 0) out vec4 outValue;

float pressureAt(vec2 uv, float centre) {
    return texture(obstacles, uv).r > 0.5 ? centre : texture(pressureField, uv).r;
}

void main() {
    vec2 t = g.texel.xy;
    float centre = texture(pressureField, vUV).r;
    float sum = pressureAt(vUV - vec2(t.x, 0.0), centre) + pressureAt(vUV + vec2(t.x, 0.0), centre) +
                pressureAt(vUV - vec2(0.0, t.y), centre) + pressureAt(vUV + vec2(0.0, t.y), centre);
    float divergence = texture(divergenceField, vUV).r;
    outValue = vec4((sum - divergence) * 0.25, 0.0, 0.0, 1.0);
}
