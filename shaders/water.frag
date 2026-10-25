#version 450

// Water (ADR-0018): copies the lit scene and, below each column's surface, refracts it with a
// stepped sideways wobble, mirrors the scene above the surface, and blends in the water colour lit
// by the same lights. Which pixel is read is decided with integers only (identical on every
// backend); the column data holds whole canvas rows.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Water {
    vec4 canvas;    // xy: size, z: animation phase (whole number), w: light bands
    vec4 ambient;   // rgb, a: ordered dither on / off
    vec4 color[4];  // rgb tint, a: opacity at the surface
    vec4 params[4]; // x: reflection, y: refraction px, z: opacity added per px of depth
} water;

layout(set = 1, binding = 0) uniform sampler2D scene;   // lit canvas
layout(set = 1, binding = 1) uniform sampler2D light;   // light accumulation
layout(set = 1, binding = 2) uniform sampler2D columns; // row 0 still water, row 1 a fall; surface, bottom, style, present

layout(location = 0) out vec4 outColor;

const int kWobble[8] = int[8](0, 0, 1, 1, 0, 0, -1, -1);

float bayer4(ivec2 p) {
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    return (m[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0;
}

vec3 sceneAt(ivec2 p, ivec2 size) {
    return texelFetch(scene, clamp(p, ivec2(0), size - 1), 0).rgb;
}

float quantize(float value, ivec2 p) {
    float bands = water.canvas.w;
    if (bands <= 0.0) {
        return value;
    }
    float threshold = water.ambient.a > 0.5 ? bayer4(p) : 0.5;
    return floor(value * bands + threshold) / bands;
}

void main() {
    ivec2 size = ivec2(water.canvas.xy);
    ivec2 p = ivec2(vUV * water.canvas.xy);
    int phase = int(water.canvas.z);
    vec4 fall = texelFetch(columns, ivec2(p.x, 1), 0);
    int fallTop = int(fall.r * 255.0 + 0.5);
    int fallBottom = int(fall.g * 255.0 + 0.5);
    int fallMark = int(fall.a * 255.0 + 0.5);
    if (fallMark >= 128 && p.y >= fallTop && p.y < fallBottom) {
        int style = min(int(fall.b * 255.0 + 0.5), 3);
        int band = (p.y - phase * 2) & 7;
        vec3 lightHere = water.ambient.rgb + texelFetch(light, p, 0).rgb;
        vec3 tint = water.color[style].rgb * (lightHere * 1.4 + 0.08);
        float opacity = band < 3 ? 0.78 : 0.42;
        outColor = vec4(mix(sceneAt(p, size), tint, opacity), 1.0);
        return;
    }

    vec4 column = texelFetch(columns, ivec2(p.x, 0), 0);
    int surface = int(column.r * 255.0 + 0.5);
    int bottom = int(column.g * 255.0 + 0.5);
    int style = min(int(column.b * 255.0 + 0.5), 3);
    int mark = int(column.a * 255.0 + 0.5);
    if (mark < 128 || p.y < surface || p.y >= bottom) {
        outColor = vec4(sceneAt(p, size), 1.0);
        return;
    }

    int depth = p.y - surface;
    if (mark != 255) {
        // Bands travel down the sheet. The phase steps by whole pixels with time.
        int band = (p.y - phase * 2) & 7;
        vec3 lightHere = water.ambient.rgb + texelFetch(light, p, 0).rgb;
        vec3 tint = water.color[style].rgb * (lightHere * 1.4 + 0.08);
        float opacity = band < 3 ? 0.78 : 0.42;
        outColor = vec4(mix(sceneAt(p, size), tint, opacity), 1.0);
        return;
    }

    int wobble = kWobble[(p.y + phase) & 7] * int(water.params[style].y);
    vec3 under = sceneAt(ivec2(p.x + wobble, p.y), size);

    vec3 lightHere = water.ambient.rgb + texelFetch(light, p, 0).rgb;
    vec3 tint = water.color[style].rgb * lightHere;
    float opacity = quantize(clamp(water.color[style].a + float(depth) * water.params[style].z, 0.0, 0.92), p);
    vec3 color = mix(under, tint, opacity);

    int mirrorRow = surface - 1 - depth;
    if (mirrorRow >= 0) {
        float strength = quantize(water.params[style].x * clamp(1.0 - float(depth) / 16.0, 0.0, 1.0), p);
        color = mix(color, sceneAt(ivec2(p.x - wobble, mirrorRow), size), strength);
    }
    if (depth == 0) {
        // The surface line catches light.
        color = mix(color, water.color[style].rgb * (lightHere * 1.8 + 0.12), 0.65);
    }
    outColor = vec4(color, 1.0);
}
