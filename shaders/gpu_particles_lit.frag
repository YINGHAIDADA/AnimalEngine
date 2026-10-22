#version 450

// GPU particles in the material pass: lit like any sprite (flat normal, no emission, no rim).

layout(location = 0) in vec4 vColor; // premultiplied

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outEmissive;
layout(location = 3) out vec4 outFlags;

void main() {
    float coverage = vColor.a;
    outAlbedo = vColor;
    outNormal = vec4(vec3(0.5, 0.5, 1.0) * coverage, coverage);
    outEmissive = vec4(0.0, 0.0, 0.0, coverage);
    outFlags = vec4(0.0, 0.0, 0.0, coverage);
}
