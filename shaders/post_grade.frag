#version 450

// Final post-process: adds pixel-quantized bloom, then color grading and vignette.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Grade {
    vec4 bloom;         // x: intensity (0 = off), y: bands (0 = smooth)
    vec4 grade;         // x: exposure, y: contrast, z: saturation, w: vignette
    vec4 shadowTint;    // rgb
    vec4 highlightTint; // rgb
    vec4 canvasSize;    // xy in pixels
} u;

layout(set = 1, binding = 0) uniform sampler2D canvas;    // nearest
layout(set = 1, binding = 1) uniform sampler2D bloomField; // linear, half resolution

layout(location = 0) out vec4 outColor;

float bayer4(ivec2 p) {
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    return (m[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0;
}

void main() {
    vec3 color = texture(canvas, vUV).rgb;

    vec3 glow = texture(bloomField, vUV).rgb * u.bloom.x;
    float level = max(glow.r, max(glow.g, glow.b));
    if (u.bloom.y > 0.0 && level > 0.0) {
        // Quantize the halo with ordered dither so it reads as pixel art, like the lighting.
        float bands = u.bloom.y;
        float quantized = floor(level * bands + bayer4(ivec2(vUV * u.canvasSize.xy))) / bands;
        glow *= quantized / level;
    }
    color += glow;

    color *= u.grade.x;
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    color *= mix(u.shadowTint.rgb, u.highlightTint.rgb, clamp(luminance, 0.0, 1.0));
    luminance = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luminance), color, u.grade.z);
    color = (color - 0.5) * u.grade.y + 0.5;

    vec2 centred = (vUV - 0.5) * vec2(u.canvasSize.x / u.canvasSize.y, 1.0);
    float distance = length(centred) / length(vec2(u.canvasSize.x / u.canvasSize.y, 1.0) * 0.5);
    color *= 1.0 - u.grade.w * distance * distance;

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
