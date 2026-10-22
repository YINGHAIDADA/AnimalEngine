#version 450

// Additive point-light contribution with normal mapping (Lambert, quadratic falloff) and hard
// pixel shadows from the occluder mask (ADR-0012).

layout(location = 0) in vec2 vWorld;
layout(location = 1) in vec2 vCenter;
layout(location = 2) in vec4 vColor;
layout(location = 3) in vec4 vParams;
layout(location = 4) in vec2 vScreenUV;
layout(location = 5) flat in ivec2 vLightTexel;
layout(location = 6) flat in vec4 vSpot;

layout(set = 0, binding = 0, std140) uniform View {
    mat4 viewProjection;
    vec4 params; // xy: canvas size, z: shadows enabled, w: penetration in pixels
    vec4 rim;    // x: rim intensity
} view;

layout(set = 1, binding = 0) uniform sampler2D normalBuffer;
layout(set = 1, binding = 1) uniform sampler2D occluders;
layout(set = 1, binding = 2) uniform sampler2D materialFlags; // r: rim strength

layout(location = 0) out vec4 outLight;
layout(location = 1) out vec4 outRim;

float rimMask(ivec2 texel, ivec2 size) {
    if (any(lessThan(texel, ivec2(0))) || any(greaterThanEqual(texel, size))) {
        return 0.0;
    }
    return texelFetch(materialFlags, texel, 0).r;
}

// The neighbour of `pixel` in the direction of `light`, by integer comparisons only (identical on
// every backend): diagonal unless the direction is within ~27° of an axis.
ivec2 towards(ivec2 pixel, ivec2 light) {
    ivec2 d = light - pixel;
    ivec2 a = abs(d);
    ivec2 step = ivec2(sign(vec2(d)));
    return pixel + ivec2(a.x * 2 > a.y ? step.x : 0, a.y * 2 > a.x ? step.y : 0);
}

bool occluded(ivec2 texel, ivec2 size) {
    if (any(lessThan(texel, ivec2(0))) || any(greaterThanEqual(texel, size))) {
        return false;
    }
    return texelFetch(occluders, texel, 0).r > 0.5;
}

// Fraction of the light reaching this pixel. Walk the pixels from the light to this one (integer
// Bresenham, so every backend visits exactly the same pixels); occluder pixels around the light
// itself (a light inside a crystal tile) are skipped. A ray that passes through an occluder and out
// again is blocked, so shadow edges on open space are hard and walls never leak. Inside the
// occluder the pixel belongs to, light fades over `penetration` pixels: the face towards the light
// is lit and darkens inwards (banded by the composite).
float transmittance(ivec2 from, ivec2 to, float penetration) {
    ivec2 size = ivec2(view.params.xy);
    ivec2 d = abs(to - from);
    ivec2 s = ivec2(sign(vec2(to - from)));
    int steps = max(d.x, d.y);
    float stepLength = steps > 0 ? length(vec2(to - from)) / float(steps) : 0.0;
    int err = d.x - d.y;
    ivec2 p = from;
    bool leftSource = false;
    float depth = 0.0;
    for (int i = 0; i <= steps; ++i) {
        if (occluded(p, size)) {
            depth += leftSource ? stepLength : 0.0;
        } else if (depth > 0.0) {
            return 0.0; // came out of an occluder: blocked
        } else {
            leftSource = true;
        }
        int e2 = 2 * err;
        if (e2 > -d.y) {
            err -= d.y;
            p.x += s.x;
        }
        if (e2 < d.x) {
            err += d.x;
            p.y += s.y;
        }
    }
    return penetration > 0.0 ? clamp(1.0 - depth / penetration, 0.0, 1.0) : (depth > 0.0 ? 0.0 : 1.0);
}

void main() {
    outLight = vec4(0.0);
    outRim = vec4(0.0);
    vec2 toLight = vCenter - vWorld;
    float radius = vParams.x;
    float distance = length(toLight);
    if (distance >= radius) {
        return;
    }
    ivec2 size = ivec2(view.params.xy);
    ivec2 pixel = ivec2(floor(vScreenUV * view.params.xy));
    float visible = 1.0;
    if (view.params.z > 0.5 && vParams.z > 0.5) {
        visible = transmittance(vLightTexel, pixel, view.params.w);
        if (visible <= 0.0) {
            return;
        }
    }
    float falloff = 1.0 - distance / radius;
    falloff *= falloff;
    if (vSpot.z >= -1.0) {
        float along = distance > 0.0 ? dot(-toLight / distance, vSpot.xy) : 1.0;
        falloff *= smoothstep(vSpot.z, vSpot.w, along);
        if (falloff <= 0.0) {
            return;
        }
    }

    vec3 n = texture(normalBuffer, vScreenUV).rgb * 2.0 - 1.0;
    n = dot(n, n) > 1e-6 ? normalize(n) : vec3(0.0, 0.0, 1.0);
    vec3 l = normalize(vec3(toLight, vParams.y));
    float diffuse = max(dot(n, l), 0.0);
    outLight = vec4(vColor.rgb * (diffuse * falloff * visible), 0.0);

    // Rim: a rim sprite's pixel whose neighbour towards the light is not part of a rim sprite is on
    // the silhouette facing the light.
    float rim = rimMask(pixel, size);
    if (rim > 0.0 && pixel != vLightTexel && rimMask(towards(pixel, vLightTexel), size) <= 0.0) {
        outRim = vec4(vColor.rgb * (rim * view.rim.x * falloff * visible), 0.0);
    }
}
