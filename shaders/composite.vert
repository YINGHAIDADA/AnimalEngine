#version 450

// Fullscreen triangle; vertices (-1,-1), (3,-1), (-1,3) in NDC.

layout(location = 0) in vec2 inPosition;

layout(location = 0) out vec2 vUV;

void main() {
    vUV = inPosition * 0.5 + 0.5;
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
