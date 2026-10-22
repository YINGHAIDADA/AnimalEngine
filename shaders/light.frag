#version 450

// Additive point-light contribution with normal mapping (Lambert, quadratic falloff).

layout(location = 0) in vec2 vWorld;
layout(location = 1) in vec2 vCenter;
layout(location = 2) in vec4 vColor;
layout(location = 3) in vec2 vParams;
layout(location = 4) in vec2 vScreenUV;

layout(set = 1, binding = 0) uniform sampler2D normalBuffer;

layout(location = 0) out vec4 outLight;

void main() {
    vec2 toLight = vCenter - vWorld;
    float radius = vParams.x;
    float distance = length(toLight);
    if (distance >= radius) {
        outLight = vec4(0.0);
        return;
    }
    float falloff = 1.0 - distance / radius;
    falloff *= falloff;

    vec3 n = texture(normalBuffer, vScreenUV).rgb * 2.0 - 1.0;
    n = dot(n, n) > 1e-6 ? normalize(n) : vec3(0.0, 0.0, 1.0);
    vec3 l = normalize(vec3(toLight, vParams.y));
    float diffuse = max(dot(n, l), 0.0);

    outLight = vec4(vColor.rgb * (diffuse * falloff), 0.0);
}
