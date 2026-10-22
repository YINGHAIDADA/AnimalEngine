#version 450

// One quad per point light, in world pixels.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inCenter;
layout(location = 2) in vec4 inColor;  // rgb premultiplied by intensity
layout(location = 3) in vec4 inParams; // x: radius, y: height above the surface, z: casts shadows
layout(location = 4) in vec2 inTexel;  // canvas pixel of the light, whole numbers
layout(location = 5) in vec4 inSpot;   // xy: cone direction, z / w: cosine of the outer / inner edge (z < -1: omni)

layout(set = 0, binding = 0, std140) uniform View {
    mat4 viewProjection;
    vec4 params; // xy: canvas size, z: shadows enabled, w: penetration in pixels
} view;

layout(location = 0) out vec2 vWorld;
layout(location = 1) out vec2 vCenter;
layout(location = 2) out vec4 vColor;
layout(location = 3) out vec4 vParams;
layout(location = 4) out vec2 vScreenUV;
layout(location = 5) flat out ivec2 vLightTexel;
layout(location = 6) flat out vec4 vSpot;

void main() {
    vec4 clip = view.viewProjection * vec4(inPosition, 0.0, 1.0);
    gl_Position = clip;
    // RHI convention: NDC (-1, -1) is the top-left texel, so this maps straight to texture uv.
    vScreenUV = clip.xy * 0.5 + 0.5;
    vLightTexel = ivec2(inTexel);
    vSpot = inSpot;
    vWorld = inPosition;
    vCenter = inCenter;
    vColor = inColor;
    vParams = inParams;
}
