#pragma once

#include <animal/platform/input.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace animal {

// --input-script=path: input events injected at given frames (automated UI tests and demos).
//   # comment
//   <frame> key <Key>              press and release (released on the next frame)
//   <frame> down|up <Key>          hold / release a key (e.g. Shortcut, LeftShift)
//   <frame> down|up left|right|middle
//   <frame> move <x> <y>           mouse to the centre of canvas pixel (x, y)
//   <frame> click <x> <y> [left|right|middle]
//   <frame> text <characters...>
// Keys are the platform::Key names; "Shortcut" is Command on macOS and Ctrl elsewhere.
// --record-input=path: the keys pressed and released while playing, written as an input script
// on exit (replays identically with --deterministic, which also records in step with the frames).
class InputRecorder {
public:
    void record(uint64_t frame, const std::vector<platform::InputEvent>& events);
    bool save(const std::string& path) const; // logs errors

private:
    std::string text_ = "# recorded with --record-input\n";
    std::vector<bool> held_ = std::vector<bool>(static_cast<size_t>(platform::Key::Count), false);
};

class InputScript {
public:
    static std::optional<InputScript> load(const std::string& path); // logs errors

    // Canvas pixel -> window point.
    using ToPoints = std::function<platform::MousePosition(float x, float y)>;
    // Appends the events of `frame` (marked synthetic).
    void emit(uint64_t frame, const ToPoints& toPoints, std::vector<platform::InputEvent>& out);
    uint64_t lastFrame() const { return steps_.empty() ? 0 : steps_.back().frame; }

private:
    struct Step {
        uint64_t frame = 0;
        platform::InputEvent event; // positions in canvas pixels until emitted
    };

    std::vector<Step> steps_;
    size_t next_ = 0;
    platform::KeyModifiers held_;
};

} // namespace animal
