#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace animal::platform {

enum class Key : uint8_t {
    Left,
    Right,
    Up,
    Down,
    W,
    A,
    S,
    D,
    Space,
    Enter,
    Escape,
    F1,
    F12,
    Count,
};

class Input {
public:
    // Samples the keyboard; call once per frame after pollEvents().
    void update();

    bool down(Key key) const { return current_[index(key)]; }
    bool pressed(Key key) const { return current_[index(key)] && !previous_[index(key)]; }

private:
    static constexpr size_t index(Key key) { return static_cast<size_t>(key); }

    std::array<bool, static_cast<size_t>(Key::Count)> current_{};
    std::array<bool, static_cast<size_t>(Key::Count)> previous_{};
};

} // namespace animal::platform
