#version 450

// color = albedo * (ambient + lights) + emissive, with optional pixel-art light banding,
// then smoke (from the fluid simulation) blended over it, lit by the same light.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Composite {
    vec4 ambient;    // rgb
    vec4 params;     // x: lighting enabled, y: light bands (0 = smooth), z: ordered dither
    vec4 canvasSize; // xy in pixels
    vec4 smoke;      // rgb: tint, a: opacity scale
    vec4 rimAmbient; // rgb
} settings;

layout(set = 1, binding = 0) uniform sampler2D albedoBuffer;
layout(set = 1, binding = 1) uniform sampler2D lightBuffer;
layout(set = 1, binding = 2) uniform sampler2D emissiveBuffer;
layout(set = 1, binding = 3) uniform sampler2D smokeField; // linear; rgb = color × density, a = density
layout(set = 1, binding = 4) uniform sampler2D rimBuffer;  // rim light from the light pass
layout(set = 1, binding = 5) uniform sampler2D materialFlags; // r: rim strength

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
    float threshold = 0.5;
    if (bands > 0.0 && level > 0.0) {
        threshold = settings.params.z > 0.5 ? bayer4(ivec2(vUV * settings.canvasSize.xy)) : 0.5;
        float quantized = floor(level * bands + threshold) / bands;
        light *= quantized / level;
    }
    vec3 lit = albedo.rgb * light + emissive;

    // Rim light is added regardless of albedo: the dark outline of a sprite is what catches it.
    // Ambient rim: top and side silhouette edges (not the bottom, which usually stands on something).
    if (settings.params.x > 0.5) {
        ivec2 p = ivec2(vUV * settings.canvasSize.xy);
        ivec2 size = ivec2(settings.canvasSize.xy);
        float rim = texelFetch(materialFlags, p, 0).r;
        if (rim > 0.0) {
            float up = p.y > 0 ? texelFetch(materialFlags, p + ivec2(0, -1), 0).r : 0.0;
            float left = p.x > 0 ? texelFetch(materialFlags, p + ivec2(-1, 0), 0).r : 0.0;
            float right = p.x + 1 < size.x ? texelFetch(materialFlags, p + ivec2(1, 0), 0).r : 0.0;
            if (min(up, min(left, right)) <= 0.0) {
                lit += settings.rimAmbient.rgb * rim;
            }
        }
        lit += texture(rimBuffer, vUV).rgb;
    }

    vec4 dye = texture(smokeField, vUV);
    float density = clamp(dye.a * settings.smoke.a, 0.0, 1.0);
    if (bands > 0.0) {
        // Quantize smoke opacity too, with the same dither, so it reads as pixel art.
        density = floor(density * bands + threshold) / bands;
    }
    vec3 smokeColor = dye.a > 1e-4 ? dye.rgb / dye.a : vec3(0.0);
    vec3 smoke = smokeColor * settings.smoke.rgb * light;

    outColor = vec4(mix(lit, smoke, clamp(density, 0.0, 1.0)), 1.0);
}
