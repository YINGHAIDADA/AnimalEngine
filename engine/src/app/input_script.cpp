#include "input_script.h"

#include <animal/core/file.h>
#include <animal/core/log.h>

#include <algorithm>
#include <format>
#include <sstream>

namespace animal {

namespace {

using platform::InputEvent;
using platform::Key;
using platform::MouseButton;

std::optional<MouseButton> buttonFromName(const std::string& name) {
    if (name == "left") {
        return MouseButton::Left;
    }
    if (name == "right") {
        return MouseButton::Right;
    }
    if (name == "middle") {
        return MouseButton::Middle;
    }
    return std::nullopt;
}

void trackModifier(platform::KeyModifiers& held, Key key, bool down) {
    switch (key) {
    case Key::LeftShift:
    case Key::RightShift:
        held.shift = down;
        break;
    case Key::LeftCtrl:
    case Key::RightCtrl:
        held.ctrl = down;
        break;
    case Key::LeftAlt:
    case Key::RightAlt:
        held.alt = down;
        break;
    case Key::LeftSuper:
    case Key::RightSuper:
        held.super = down;
        break;
    default:
        break;
    }
}

} // namespace

std::optional<InputScript> InputScript::load(const std::string& path) {
    std::string error;
    const auto bytes = readFile(path, &error);
    if (!bytes) {
        log::error("--input-script: cannot read {}: {}", path, error);
        return std::nullopt;
    }
    InputScript script;
    std::istringstream in(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    std::string line;
    int lineNumber = 0;
    const auto fail = [&](const std::string& message) {
        log::error("{}:{}: {}", path, lineNumber, message);
        return std::nullopt;
    };
    while (std::getline(in, line)) {
        ++lineNumber;
        if (const size_t hash = line.find('#'); hash != std::string::npos) {
            line.resize(hash);
        }
        std::istringstream words(line);
        uint64_t frame = 0;
        std::string verb;
        if (!(words >> frame >> verb)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos) {
                continue;
            }
            return fail("expected <frame> <action> ...");
        }
        const auto add = [&](uint64_t f, InputEvent e) {
            e.synthetic = true;
            script.steps_.push_back({f, std::move(e)});
        };
        if (verb == "key" || verb == "down" || verb == "up") {
            std::string name;
            words >> name;
            InputEvent e;
            if (const auto button = buttonFromName(name); button && verb != "key") {
                e.type = InputEvent::Type::MouseButton;
                e.button = *button;
                e.down = verb == "down";
                add(frame, e);
                continue;
            }
            const std::optional<Key> key = platform::keyFromName(name);
            if (!key) {
                return fail("unknown key '" + name + "'");
            }
            e.type = InputEvent::Type::Key;
            e.key = *key;
            e.down = verb != "up";
            add(frame, e);
            if (verb == "key") {
                e.down = false;
                add(frame + 1, e);
            }
        } else if (verb == "move" || verb == "click") {
            InputEvent move;
            move.type = InputEvent::Type::MouseMove;
            if (!(words >> move.x >> move.y)) {
                return fail("expected canvas coordinates");
            }
            add(frame, move);
            if (verb == "click") {
                std::string name = "left";
                words >> name;
                const auto button = buttonFromName(name);
                if (!button) {
                    return fail("unknown mouse button '" + name + "'");
                }
                InputEvent press;
                press.type = InputEvent::Type::MouseButton;
                press.button = *button;
                press.down = true;
                add(frame, press);
                press.down = false;
                add(frame + 1, press);
            }
        } else if (verb == "text") {
            InputEvent e;
            e.type = InputEvent::Type::Text;
            std::getline(words >> std::ws, e.text);
            add(frame, e);
        } else {
            return fail("unknown action '" + verb + "'");
        }
    }
    std::stable_sort(script.steps_.begin(), script.steps_.end(),
                     [](const Step& a, const Step& b) { return a.frame < b.frame; });
    log::info("Input script {}: {} event(s) until frame {}", path, script.steps_.size(), script.lastFrame());
    return script;
}

void InputScript::emit(uint64_t frame, const ToPoints& toPoints, std::vector<InputEvent>& out) {
    while (next_ < steps_.size() && steps_[next_].frame <= frame) {
        InputEvent e = steps_[next_++].event;
        if (e.type == InputEvent::Type::MouseMove) {
            const platform::MousePosition p = toPoints(e.x, e.y);
            e.x = p.x;
            e.y = p.y;
        } else if (e.type == InputEvent::Type::Key) {
            trackModifier(held_, e.key, e.down);
            e.modifiers = held_;
        }
        out.push_back(std::move(e));
    }
}

void InputRecorder::record(uint64_t frame, const std::vector<platform::InputEvent>& events) {
    for (const platform::InputEvent& e : events) {
        if (e.type != platform::InputEvent::Type::Key || e.synthetic || e.key == Key::Count) {
            continue;
        }
        const auto index = static_cast<size_t>(e.key);
        if (held_[index] == e.down) {
            continue; // key repeat
        }
        held_[index] = e.down;
        text_ += std::format("{} {} {}\n", frame, e.down ? "down" : "up", platform::keyName(e.key));
    }
}

bool InputRecorder::save(const std::string& path) const {
    if (!writeFileAtomic(path, text_.data(), text_.size())) {
        log::error("--record-input: cannot write {}", path);
        return false;
    }
    log::info("Recorded input written to {}", path);
    return true;
}

} // namespace animal
