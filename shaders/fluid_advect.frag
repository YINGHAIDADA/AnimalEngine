#version 450

// Semi-Lagrangian advection of `sourceField` through the velocity field (cells per second),
// with dissipation and optional buoyancy (smoke density pushes velocity upwards).

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Advect {
    vec4 texel;  // xy: 1 / grid size
    vec4 params; // x: dt, y: dissipation factor for this step, z: buoyancy (cells/s² per density)
} u;

layout(set = 1, binding = 0) uniform sampler2D velocityField; // linear
layout(set = 1, binding = 1) uniform sampler2D sourceField;   // linear
layout(set = 1, binding = 2) uniform sampler2D obstacles;     // nearest; r > 0.5 = solid
layout(set = 1, binding = 3) uniform sampler2D densityField;  // linear; a = smoke density

layout(location = 0) out vec4 outValue;

void main() {
    if (texture(obstacles, vUV).r > 0.5) {
        outValue = vec4(0.0);
        return;
    }
    vec2 velocity = texture(velocityField, vUV).xy;
    vec2 origin = vUV - u.params.x * velocity * u.texel.xy;
    vec4 value = texture(sourceField, origin) * u.params.y;
    // +y points down in the grid, so rising smoke means negative y velocity.
    value.y -= u.params.z * texture(densityField, vUV).a * u.params.x;
    outValue = value;
}
