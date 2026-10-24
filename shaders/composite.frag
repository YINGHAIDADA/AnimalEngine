#version 450

// color = albedo * (ambient + lights + indirect) + emissive, with optional pixel-art light banding,
// then smoke (from the fluid simulation) blended over it, lit by the same light. Depth (ADR-0039):
// ambient occlusion and a top-to-bottom gradient shape the ambient light; the back wall takes less
// direct light and is pulled towards the depth fog colour.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Composite {
    vec4 ambient;    // rgb
    vec4 params;     // x: lighting enabled, y: light bands (0 = smooth), z: ordered dither
    vec4 canvasSize; // xy in pixels
    vec4 smoke;      // rgb: tint, a: opacity scale
    vec4 rimAmbient; // rgb
    vec4 depth;      // x: back-wall light, y: depth fog, z: ambient occlusion, w: ambient gradient
    vec4 fog;        // rgb: depth fog colour, a: indirect light strength
} settings;

layout(set = 1, binding = 0) uniform sampler2D albedoBuffer;
layout(set = 1, binding = 1) uniform sampler2D lightBuffer;
layout(set = 1, binding = 2) uniform sampler2D emissiveBuffer;
layout(set = 1, binding = 3) uniform sampler2D smokeField; // linear; rgb = color × density, a = density
layout(set = 1, binding = 4) uniform sampler2D rimBuffer;  // rim light from the light pass
layout(set = 1, binding = 5) uniform sampler2D materialFlags; // r: rim strength, g: depth (1 = back wall)
layout(set = 1, binding = 6) uniform sampler2D indirectBuffer; // a quarter of the canvas
layout(set = 1, binding = 7) uniform sampler2D ambientOcclusion; // r: 0 open .. 1 enclosed

layout(location = 0) out vec4 outColor;

float bayer4(ivec2 p) {
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    return (m[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0;
}

// Bilinear by hand, from integer pixel coordinates: hardware filtering and interpolated UVs differ
// slightly between backends. Indirect texel i averages canvas pixels 4i .. 4i + 3.
vec3 indirectLight(ivec2 pixel) {
    ivec2 size = textureSize(indirectBuffer, 0);
    vec2 position = (vec2(pixel) + 0.5) * 0.25 - 0.5;
    ivec2 base = ivec2(floor(position));
    vec2 f = position - vec2(base);
    ivec2 last = size - 1;
    vec3 a = texelFetch(indirectBuffer, clamp(base, ivec2(0), last), 0).rgb;
    vec3 b = texelFetch(indirectBuffer, clamp(base + ivec2(1, 0), ivec2(0), last), 0).rgb;
    vec3 c = texelFetch(indirectBuffer, clamp(base + ivec2(0, 1), ivec2(0), last), 0).rgb;
    vec3 d = texelFetch(indirectBuffer, clamp(base + ivec2(1, 1), ivec2(0), last), 0).rgb;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec4 albedo = texture(albedoBuffer, vUV);
    vec3 emissive = texture(emissiveBuffer, vUV).rgb;
    ivec2 texel = ivec2(vUV * settings.canvasSize.xy);
    float depth = texelFetch(materialFlags, texel, 0).g;
    float open = 1.0 - texelFetch(ambientOcclusion, texel, 0).r * settings.depth.z;
    float height = (float(texel.y) + 0.5) / settings.canvasSize.y;
    float gradient = max(1.0 + settings.depth.w * (1.0 - 2.0 * height), 0.0);
    vec3 direct = texture(lightBuffer, vUV).rgb * mix(1.0, settings.depth.x, depth);
    vec3 indirect = indirectLight(texel) * settings.fog.a;
    vec3 light = settings.ambient.rgb * gradient * open + direct + indirect * open;
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
    vec3 lit = mix(albedo.rgb * light, settings.fog.rgb, settings.depth.y * depth) + emissive;

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
