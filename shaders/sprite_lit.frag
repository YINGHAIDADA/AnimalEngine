#version 450

// Material pass: writes albedo, normal and emissive into the G-buffer (premultiplied by coverage).
// A fourth target holds material flags: r = rim-light strength (ADR-0014).

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vParams; // x: rim strength

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;   // premultiplied alpha
layout(set = 1, binding = 1) uniform sampler2D normalTexture;   // rgb = n * 0.5 + 0.5; +x right, +y down, +z out
layout(set = 1, binding = 2) uniform sampler2D emissiveTexture; // rgb added after lighting

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outEmissive;
layout(location = 3) out vec4 outFlags;

void main() {
    vec4 albedo = texture(albedoTexture, vUV) * vColor;
    float coverage = albedo.a;

    vec3 encoded = texture(normalTexture, vUV).rgb;
    // Horizontally mirrored sprites have decreasing u across the screen; mirror the normal too.
    if (dFdx(vUV.x) < 0.0) {
        encoded.x = 1.0 - encoded.x;
    }
    vec3 emissive = texture(emissiveTexture, vUV).rgb;

    outAlbedo = albedo;
    outNormal = vec4(encoded * coverage, coverage);
    outEmissive = vec4(emissive * coverage, coverage);
    outFlags = vec4(vParams.x * coverage, 0.0, 0.0, coverage);
}
