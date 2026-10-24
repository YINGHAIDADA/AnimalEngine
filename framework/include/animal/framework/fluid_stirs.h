#pragma once

// Moving bodies stirring the smoke (ADR-0008): which of them get one of the solver's few force
// splats, and how hard they push. Smoke itself is not added.

#include <animal/core/math.h>
#include <animal/framework/foliage.h>
#include <animal/renderer/fluid.h>

#include <span>
#include <vector>

namespace animal {

struct StirBody {
    Vec2 position; // centre, world pixels
    Vec2 velocity; // px/s
    float radius = 6.0f;
};

struct StirParams {
    float coupling = 3.0f;     // 1/s: the force (px/s²) is the velocity times this
    float minSpeed = 8.0f;     // px/s: slower bodies are settling and get no splat
    float vineRadius = 5.0f;
    float vineSpacing = 12.0f; // px of vine per sample, which is the fastest point along it
};

// One splat per body, then one per `vineSpacing` of each vine (velocities from the last substep,
// `vineSubstep` seconds; none when 0). At most `budget`: bodies before vine samples, the fastest of
// each first.
std::vector<FluidSplat> fluidStirs(std::span<const StirBody> bodies, std::span<const VineSystem::Vine> vines,
                                   float vineSubstep, const StirParams& params, size_t budget);

} // namespace animal
