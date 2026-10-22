#version 450

// One quad per point light, in world pixels.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inCenter;
layout(location = 2) in vec4 inColor;  // rgb premultiplied by intensity
layout(location = 3) in vec2 inParams; // x: radius, y: height above the surface

layout(set = 0, binding = 0, std140) uniform View {
    mat4 viewProjection;
} view;

layout(location = 0) out vec2 vWorld;
layout(location = 1) out vec2 vCenter;
layout(location = 2) out vec4 vColor;
layout(location = 3) out vec2 vParams;
layout(location = 4) out vec2 vScreenUV;

void main() {
    vec4 clip = view.viewProjection * vec4(inPosition, 0.0, 1.0);
    gl_Position = clip;
    // RHI convention: NDC (-1, -1) is the top-left texel, so this maps straight to texture uv.
    vScreenUV = clip.xy * 0.5 + 0.5;
    vWorld = inPosition;
    vCenter = inCenter;
    vColor = inColor;
    vParams = inParams;
}
