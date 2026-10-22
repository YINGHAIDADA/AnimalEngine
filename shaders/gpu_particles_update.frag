#version 450

// GPU particles (ADR-0020): one texel per particle slot. Advances age, respawns dead slots inside
// their emitter's area, integrates motion with wind and drag, and kills particles that hit solid
// tiles, cross the emitter's floor line or leave the canvas. Randomness is an integer hash of
// (slot, frame), so every backend computes the same particles.

layout(location = 0) in vec2 vUV;

struct Emitter {
    vec4 area;       // world px: x, y, width, height
    vec4 velocity;   // x min, x max, y min, y max (px/s)
    vec4 motion;     // acceleration x, y (px/s²), drag (1/s), wind response (px/s² per wind unit)
    vec4 life;       // lifetime min, max (s), fade (s), floor y (world px; particles die below it)
    vec4 startColor; // straight alpha
    vec4 endColor;
    vec4 slots;      // first slot, slot count, die on solid, style (0 lit, 1 glow)
};

layout(set = 0, binding = 0, std140) uniform Simulation {
    vec4 step;      // x: dt, y: reset, z: frame (whole number), w: state texture width
    vec4 canvas;    // xy: world position of the canvas' top-left pixel, zw: canvas size
    vec4 wind;      // xy: wind vector (wind units)
    Emitter emitters[4];
} sim;

layout(set = 1, binding = 0) uniform sampler2D stateA; // position xy, velocity xy
layout(set = 1, binding = 1) uniform sampler2D stateB; // age, lifetime, flags (< 0: landed on the floor), emitter
layout(set = 1, binding = 2) uniform sampler2D occluders;

layout(location = 0) out vec4 outA;
layout(location = 1) out vec4 outB;

uint hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float random(uint slot, uint salt) {
    uint frame = uint(sim.step.z);
    return float(hash(slot * 16u + salt + hash(frame + 0x9e37u)) & 0xffffffu) / 16777216.0;
}

void main() {
    ivec2 texel = ivec2(vUV * vec2(textureSize(stateA, 0)));
    uint slot = uint(texel.y) * uint(sim.step.w) + uint(texel.x);
    int e = -1;
    for (int i = 0; i < 4; ++i) {
        float first = sim.emitters[i].slots.x;
        float count = sim.emitters[i].slots.y;
        if (float(slot) >= first && float(slot) < first + count) {
            e = i;
        }
    }
    if (e < 0) {
        outA = vec4(-10000.0, -10000.0, 0.0, 0.0);
        outB = vec4(-1.0, 0.0, 0.0, -1.0);
        return;
    }
    Emitter em = sim.emitters[e];

    if (sim.step.y > 0.5) {
        // Fresh start: every slot waits a random part of a lifetime before its first spawn.
        outA = vec4(-10000.0, -10000.0, 0.0, 0.0);
        outB = vec4(-random(slot, 1u) * em.life.y, 0.0, 0.0, float(e));
        return;
    }

    vec4 a = texelFetch(stateA, texel, 0);
    vec4 b = texelFetch(stateB, texel, 0);
    float dt = sim.step.x;
    float age = b.x + dt;
    float lifetime = b.y;
    if (age < 0.0) {
        outA = a;
        outB = vec4(age, lifetime, b.z, float(e));
        return;
    }
    if (age >= lifetime || b.z < 0.0) {
        vec2 position = em.area.xy + vec2(random(slot, 2u), random(slot, 3u)) * em.area.zw;
        vec2 velocity = vec2(mix(em.velocity.x, em.velocity.y, random(slot, 4u)),
                             mix(em.velocity.z, em.velocity.w, random(slot, 5u)));
        lifetime = mix(em.life.x, em.life.y, random(slot, 6u));
        outA = vec4(position, velocity);
        outB = vec4(0.0, lifetime, 0.0, float(e));
        return;
    }

    vec2 velocity = a.zw + (em.motion.xy + sim.wind.xy * em.motion.w) * dt;
    velocity *= max(0.0, 1.0 - em.motion.z * dt);
    vec2 position = a.xy + velocity * dt;

    ivec2 pixel = ivec2(floor(position - sim.canvas.xy));
    ivec2 size = ivec2(sim.canvas.zw);
    bool outside = any(lessThan(pixel, ivec2(-16))) || any(greaterThanEqual(pixel, size + 16));
    bool solid = em.slots.z > 0.5 && all(greaterThanEqual(pixel, ivec2(0))) && all(lessThan(pixel, size)) &&
                 texelFetch(occluders, pixel, 0).r > 0.5;
    float flags = b.z;
    if (outside || solid) {
        age = lifetime; // respawns next step
    } else if (position.y >= em.life.w) {
        // Reached the floor (a water surface): drawn there this frame, respawns next step.
        position.y = em.life.w - 0.5;
        flags = -1.0;
    }
    outA = vec4(position, velocity);
    outB = vec4(age, lifetime, flags, float(e));
}
