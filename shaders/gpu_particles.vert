#version 450

// GPU particles (ADR-0020): one instance per slot, a 1×1 px quad at the particle's pixel. Slots
// that are dead or belong to an emitter of the other style collapse outside the clip volume.

layout(location = 0) in vec2 inCorner; // 0..1

struct Emitter {
    vec4 area;
    vec4 velocity;
    vec4 motion;
    vec4 life;
    vec4 startColor;
    vec4 endColor;
    vec4 slots;
};

layout(set = 0, binding = 0, std140) uniform Draw {
    mat4 viewProjection;
    vec4 params; // x: state texture width, y: style drawn (0 lit, 1 glow), z: last step's dt
    Emitter emitters[4];
} draw;

layout(set = 1, binding = 0) uniform sampler2D stateA;
layout(set = 1, binding = 1) uniform sampler2D stateB;

layout(location = 0) out vec4 vColor; // premultiplied

void main() {
    int slot = gl_InstanceIndex;
    int width = int(draw.params.x);
    ivec2 texel = ivec2(slot % width, slot / width);
    vec4 a = texelFetch(stateA, texel, 0);
    vec4 b = texelFetch(stateB, texel, 0);
    int e = int(b.w + 0.5);
    float age = b.x;
    float lifetime = b.y;
    if (b.w < -0.5 || age < 0.0 || age >= lifetime || draw.emitters[e].slots.w != draw.params.y) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vColor = vec4(0.0);
        return;
    }
    float t = clamp(age / max(lifetime, 1e-4), 0.0, 1.0);
    vec4 c = mix(draw.emitters[e].startColor, draw.emitters[e].endColor, t);
    float fade = draw.emitters[e].life.z;
    if (fade > 0.0) {
        c.a *= clamp(min(age / fade, (lifetime - age) / fade), 0.0, 1.0);
    }
    vColor = vec4(c.rgb * c.a, c.a);
    // A streak from last step's position: fast particles (falling water) stay continuous.
    vec2 now = floor(a.xy);
    vec2 before = floor(a.xy - a.zw * draw.params.z);
    vec2 lo = min(now, before);
    vec2 hi = max(now, before) + 1.0;
    lo.x = now.x; // one pixel wide, at the current column
    hi.x = now.x + 1.0;
    gl_Position = draw.viewProjection * vec4(mix(lo, hi, inCorner), 0.0, 1.0);
}
