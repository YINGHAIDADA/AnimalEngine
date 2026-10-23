#include <animal/core/assert.h>
#include <animal/framework/animation.h>

#include <cmath>

namespace animal {

namespace {

float totalDuration(const AnimationClip& clip) {
    float total = 0.0f;
    for (const float d : clip.durations) {
        total += d;
    }
    return total;
}

} // namespace

Animator::ClipId Animator::add(AnimationClip clip) {
    ANIMAL_ASSERT(!clip.frames.empty() && clip.framesPerSecond > 0.0f);
    ANIMAL_ASSERT(clip.durations.empty() || (clip.durations.size() == clip.frames.size() && totalDuration(clip) > 0.0f));
    clips_.push_back(std::move(clip));
    return clips_.size() - 1;
}

void Animator::play(ClipId clip) {
    if (clip != current_) {
        restart(clip);
    }
}

void Animator::restart(ClipId clip) {
    ANIMAL_ASSERT(clip < clips_.size());
    current_ = clip;
    time_ = 0.0f;
}

void Animator::update(float dt, float speed) {
    time_ += dt * speed;
}

int Animator::frame() const {
    if (clips_.empty()) {
        return 0;
    }
    const AnimationClip& clip = clips_[current_];
    if (!clip.durations.empty()) {
        const float total = totalDuration(clip);
        float t = clip.loop ? std::fmod(time_, total) : time_;
        for (size_t i = 0; i < clip.frames.size(); ++i) {
            if (t < clip.durations[i]) {
                return clip.frames[i];
            }
            t -= clip.durations[i];
        }
        return clip.frames.back();
    }
    const auto count = static_cast<long>(clip.frames.size());
    long index = static_cast<long>(std::floor(time_ * clip.framesPerSecond));
    index = clip.loop ? index % count : std::min(index, count - 1);
    return clip.frames[static_cast<size_t>(index)];
}

bool Animator::finished() const {
    if (clips_.empty()) {
        return true;
    }
    const AnimationClip& clip = clips_[current_];
    if (!clip.durations.empty()) {
        return !clip.loop && time_ >= totalDuration(clip);
    }
    return !clip.loop && time_ * clip.framesPerSecond >= static_cast<float>(clip.frames.size());
}

} // namespace animal
