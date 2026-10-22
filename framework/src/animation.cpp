#include <animal/core/assert.h>
#include <animal/framework/animation.h>

#include <cmath>

namespace animal {

Animator::ClipId Animator::add(AnimationClip clip) {
    ANIMAL_ASSERT(!clip.frames.empty() && clip.framesPerSecond > 0.0f);
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
    return !clip.loop && time_ * clip.framesPerSecond >= static_cast<float>(clip.frames.size());
}

} // namespace animal
