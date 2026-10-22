#version 450

// color = albedo * (ambient + lights) + emissive, with optional pixel-art light banding.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Composite {
    vec4 ambient;    // rgb
    vec4 params;     // x: lighting enabled, y: light bands (0 = smooth), z: ordered dither
    vec4 canvasSize; // xy in pixels
} settings;

layout(set = 1, binding = 0) uniform sampler2D albedoBuffer;
layout(set = 1, binding = 1) uniform sampler2D lightBuffer;
layout(set = 1, binding = 2) uniform sampler2D emissiveBuffer;

layout(location = 0) out vec4 outColor;

float bayer4(ivec2 p) {
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    return (m[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0;
}

void main() {
    vec4 albedo = texture(albedoBuffer, vUV);
    vec3 emissive = texture(emissiveBuffer, vUV).rgb;
    vec3 light = settings.ambient.rgb + texture(lightBuffer, vUV).rgb;
    if (settings.params.x < 0.5) {
        light = vec3(1.0);
    }

    float bands = settings.params.y;
    float level = max(light.r, max(light.g, light.b));
    if (bands > 0.0 && level > 0.0) {
        float threshold = settings.params.z > 0.5 ? bayer4(ivec2(vUV * settings.canvasSize.xy)) : 0.5;
        float quantized = floor(level * bands + threshold) / bands;
        light *= quantized / level;
    }

    outColor = vec4(albedo.rgb * light + emissive, 1.0);
}
