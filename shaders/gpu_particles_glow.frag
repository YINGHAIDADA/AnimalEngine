#version 450

// GPU particles in the glow pass: added over the lit scene.

layout(location = 0) in vec4 vColor; // premultiplied

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor;
}
