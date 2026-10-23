#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace animal::platform {

// Physical keys (layout-independent positions, named after the US layout).
enum class Key : uint8_t {
    Left,
    Right,
    Up,
    Down,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Space,
    Enter,
    Escape,
    Tab,
    Backspace,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,
    LeftShift,
    RightShift,
    LeftCtrl,
    RightCtrl,
    LeftAlt,
    RightAlt,
    LeftSuper, // Command on macOS, Windows key on Windows
    RightSuper,
    Grave,
    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Comma,
    Period,
    Slash,
    KeypadEnter,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Count,
};

enum class MouseButton : uint8_t {
    Left,
    Right,
    Middle,
    Count,
};

// Positional names (SDL convention): South is A on Xbox, Cross on PlayStation.
enum class GamepadButton : uint8_t {
    South,
    East,
    West,
    North,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    LeftShoulder,
    RightShoulder,
    Back,
    Start,
    Count,
};

enum class GamepadAxis : uint8_t {
    LeftX,
    LeftY,
    Count,
};

struct KeyModifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

// One OS input event of this frame, in arrival order (for UI that needs every event, such as
// text fields). Positions are in window points (see Window::sizeInPoints).
struct InputEvent {
    enum class Type : uint8_t {
        Key,         // key, down, modifiers
        Text,        // text (UTF-8)
        MouseMove,   // x, y
        MouseButton, // button, down
        MouseWheel,  // x, y: scroll amount (y > 0 scrolls up)
        Focus,       // down: the window gained focus
    };
    Type type = Type::Key;
    Key key = Key::Count; // Key::Count: a key outside the Key table
    MouseButton button = MouseButton::Left;
    bool down = false;
    KeyModifiers modifiers;
    float x = 0.0f;
    float y = 0.0f;
    std::string text;
};

struct FrameEvents {
    bool quitRequested = false;
    std::vector<InputEvent> input;
};

struct MousePosition {
    float x = 0.0f; // window points
    float y = 0.0f;
};

class Input {
public:
    static constexpr float kStickDeadzone = 0.25f;

    // Samples keyboard, mouse and the active gamepad; call once per frame after pollEvents().
    void update(const FrameEvents& events);

    // While captured (e.g. a UI text field has focus), keys / mouse buttons read as released.
    void setCaptured(bool keyboard, bool mouse) {
        keyboardCaptured_ = keyboard;
        mouseCaptured_ = mouse;
    }

    bool down(Key key) const { return !keyboardCaptured_ && keys_.current[index(key)]; }
    bool pressed(Key key) const {
        return !keyboardCaptured_ && keys_.current[index(key)] && !keys_.previous[index(key)];
    }
    KeyModifiers modifiers() const;

    MousePosition mousePosition() const { return mouse_; }
    bool down(MouseButton button) const { return !mouseCaptured_ && mouseButtons_.current[index(button)]; }
    bool pressed(MouseButton button) const {
        return !mouseCaptured_ && mouseButtons_.current[index(button)] && !mouseButtons_.previous[index(button)];
    }
    bool released(MouseButton button) const {
        return !mouseCaptured_ && !mouseButtons_.current[index(button)] && mouseButtons_.previous[index(button)];
    }
    float wheel() const { return mouseCaptured_ ? 0.0f : wheel_; } // this frame, > 0 scrolls up

    bool gamepadConnected() const { return gamepadConnected_; }
    bool down(GamepadButton button) const { return buttons_.current[index(button)]; }
    bool pressed(GamepadButton button) const {
        return buttons_.current[index(button)] && !buttons_.previous[index(button)];
    }
    // In [-1, 1] with the deadzone removed and rescaled; +Y points down.
    float axis(GamepadAxis axis) const { return axes_[index(axis)]; }

private:
    template <typename E>
    static constexpr size_t index(E value) {
        return static_cast<size_t>(value);
    }

    template <size_t N>
    struct ButtonState {
        std::array<bool, N> current{};
        std::array<bool, N> previous{};
    };

    ButtonState<static_cast<size_t>(Key::Count)> keys_;
    ButtonState<static_cast<size_t>(MouseButton::Count)> mouseButtons_;
    ButtonState<static_cast<size_t>(GamepadButton::Count)> buttons_;
    std::array<float, static_cast<size_t>(GamepadAxis::Count)> axes_{};
    MousePosition mouse_;
    float wheel_ = 0.0f;
    bool gamepadConnected_ = false;
    bool keyboardCaptured_ = false;
    bool mouseCaptured_ = false;
};

} // namespace animal::platform
