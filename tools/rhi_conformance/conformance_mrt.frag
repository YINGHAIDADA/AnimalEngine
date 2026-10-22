#version 450

// Two outputs at once: colour and its inverse.

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outInverse;

void main() {
    outColor = vec4(vColor, 1.0);
    outInverse = vec4(1.0 - vColor, 1.0);
}
