#include <animal/framework/fluid_stirs.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace animal {

namespace {

struct Sample {
    StirBody body;
    float speed = 0.0f;
};

// The fastest `cap` samples, fastest first.
void keepFastest(std::vector<Sample>& samples, size_t cap) {
    const auto keep = static_cast<std::ptrdiff_t>(std::min(cap, samples.size()));
    std::partial_sort(samples.begin(), samples.begin() + keep, samples.end(),
                      [](const Sample& a, const Sample& b) { return a.speed > b.speed; });
    samples.resize(static_cast<size_t>(keep));
}

std::vector<Sample> vineSamples(std::span<const VineSystem::Vine> vines, float substep, const StirParams& params) {
    std::vector<Sample> out;
    if (substep <= 0.0f) {
        return out;
    }
    for (const VineSystem::Vine& vine : vines) {
        std::optional<Sample> fastest; // in the stretch walked since the last sample
        float walked = 0.0f;
        for (size_t i = 1; i < vine.points.size() && i < vine.previous.size(); ++i) {
            walked += length(vine.points[i] - vine.points[i - 1]);
            const Vec2 velocity = (vine.points[i] - vine.previous[i]) * (1.0f / substep);
            const float speed = length(velocity);
            if (speed >= params.minSpeed && (!fastest || speed > fastest->speed)) {
                fastest = Sample{{vine.points[i], velocity, params.vineRadius}, speed};
            }
            if (walked >= params.vineSpacing) {
                if (fastest) {
                    out.push_back(*fastest);
                }
                fastest.reset();
                walked = 0.0f;
            }
        }
        if (fastest) {
            out.push_back(*fastest);
        }
    }
    return out;
}

} // namespace

std::vector<FluidSplat> fluidStirs(std::span<const StirBody> bodies, std::span<const VineSystem::Vine> vines,
                                   float vineSubstep, const StirParams& params, size_t budget) {
    std::vector<FluidSplat> splats;
    if (params.coupling == 0.0f || budget == 0) {
        return splats;
    }
    std::vector<Sample> moving;
    for (const StirBody& body : bodies) {
        if (const float speed = length(body.velocity); speed >= params.minSpeed) {
            moving.push_back({body, speed});
        }
    }
    keepFastest(moving, budget);
    std::vector<Sample> vine = vineSamples(vines, vineSubstep, params);
    keepFastest(vine, budget - moving.size());
    for (const std::vector<Sample>* group : {&moving, &vine}) {
        for (const Sample& sample : *group) {
            FluidSplat splat;
            splat.position = sample.body.position;
            splat.radius = sample.body.radius;
            splat.force = sample.body.velocity * params.coupling;
            splats.push_back(splat);
        }
    }
    return splats;
}

} // namespace animal
