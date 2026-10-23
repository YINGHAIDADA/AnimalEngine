#pragma once

#include <cstddef>
#include <vector>

namespace animal {

struct AnimationClip {
    std::vector<int> frames; // sprite sheet cell (or SpriteSheet frame) indices
    float framesPerSecond = 8.0f;
    bool loop = true;
    // Seconds per frame when they differ (imported animations); empty: every frame lasts
    // 1 / framesPerSecond.
    std::vector<float> durations;
};

// Plays clips frame by frame. Which clip plays is decided by the game each step (its animation
// state machine); play() is a no-op for the clip already playing so its timing continues.
class Animator {
public:
    using ClipId = size_t;

    ClipId add(AnimationClip clip);
    void play(ClipId clip);
    void restart(ClipId clip);
    void update(float dt, float speed = 1.0f); // speed scales playback (0 = paused)

    ClipId current() const { return current_; }
    int frame() const;
    bool finished() const; // a non-looping clip reached its last frame

private:
    std::vector<AnimationClip> clips_;
    ClipId current_ = 0;
    float time_ = 0.0f;
};

} // namespace animal
