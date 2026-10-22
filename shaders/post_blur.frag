#version 450

// One direction of a separable 9-tap Gaussian blur.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0, std140) uniform Blur {
    vec4 direction; // xy: one texel step in uv along the blur axis
} u;

layout(set = 1, binding = 0) uniform sampler2D source; // linear

layout(location = 0) out vec4 outValue;

void main() {
    const float weights[5] = float[5](0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);
    vec3 sum = texture(source, vUV).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = u.direction.xy * float(i);
        sum += texture(source, vUV + offset).rgb * weights[i];
        sum += texture(source, vUV - offset).rgb * weights[i];
    }
    outValue = vec4(sum, 1.0);
}
