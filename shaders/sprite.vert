#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inParams; // x: rim strength

layout(set = 0, binding = 0, std140) uniform View {
    mat4 viewProjection;
} view;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vParams;

void main() {
    vUV = inUV;
    vColor = inColor;
    vParams = inParams;
    gl_Position = view.viewProjection * vec4(inPosition, 0.0, 1.0);
}
