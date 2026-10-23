#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

// Straight alpha, unlike the sprite shaders.
layout(set = 1, binding = 0) uniform sampler2D uiTexture;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor * texture(uiTexture, vUV);
}
