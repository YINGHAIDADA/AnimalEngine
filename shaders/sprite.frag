#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

// Premultiplied alpha: texture and vertex color are both premultiplied.
layout(set = 1, binding = 0) uniform sampler2D spriteTexture;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(spriteTexture, vUV) * vColor;
}
